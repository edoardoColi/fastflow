/* ***************************************************************************
 *
 *  FastFlow is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License version 3 as
 *  published by the Free Software Foundation.
 *  Starting from version 3.0.1 FastFlow is dual licensed under the GNU LGPLv3
 *  or MIT License (https://github.com/ParaGroup/WindFlow/blob/vers3.x/LICENSE.MIT)
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 *  License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ****************************************************************************
 */
/* Author: 
 *   Nicolo' Tonci
 *   Edoardo Coli
 */

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <map>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#include <cereal/cereal.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>


#include <filesystem>
namespace n_fs = std::filesystem;

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

enum Proto {TCP = 1 , MPI};

Proto protocol;
bool shared_fs = false;
bool dry_run = false;
bool option_V = false;
char hostname[HOST_NAME_MAX];
std::vector<std::string> viewGroups;
std::map<std::string, std::string> cmd_files;   // Map of <Name of the file, Absolute path of files during local execution>
std::string argv_zero;                          // To extract the path for dff_run and dff_deployer script
std::string executable;                         // Absolute path of executable file
std::string executable_param;                   // Parameters used locally by executable file
std::string executable_remote_param;            // Parameters used remotely by executable file
std::string all_files;                          // Absolute path of all files used as parameters in command line
std::string configFile("");                     // Absolute path of JSON configuration file
std::string ssh_key_dir("");                    // Local directory for ssh key
std::string default_home_dir("");               // Default remote over home_dir in groups


static inline unsigned long getusec() {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (unsigned long)(tv.tv_sec*1e6+tv.tv_usec);
}

static inline bool toBePrinted(std::string gName){
    return (option_V || (find(viewGroups.begin(), viewGroups.end(), gName) != viewGroups.end()));
}

static inline std::vector<std::string> split (const std::string &s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string item;

    while (getline (ss, item, delim))
        result.push_back (item);

    return result;
}

static inline void convertToIP(const char *host, char *ip) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // Stream socket
    hints.ai_flags = 0;
    hints.ai_protocol = IPPROTO_TCP; // Allow only TCP
    if (getaddrinfo(host, NULL, NULL, &result) != 0) {
        perror("getaddrinfo");
        std::cerr << "FATAL ERROR\n";							
        return;
    }	
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        struct sockaddr_in *h = (struct sockaddr_in *) rp->ai_addr;
        if (inet_ntop(AF_INET, &(h->sin_addr), ip, INET_ADDRSTRLEN) == NULL) {
            perror("inet_ntop");
            continue;
        }		
        free(result);
        return;
    }
    free(result);
    std::cerr << "FATAL ERROR\n";
}


struct G {
    std::string name, endpoint, home_dir, pass_dl, rm_dl, my_dl, pass_all_files, rm_files, files; // Configuration parameters in json file for each Group
    std::string lib_dir, in_dir;
    std::string user, host, port;
    int fd = 0;
    FILE* file = nullptr;

    template <class Archive>
    void load( Archive & ar ){
        ar(cereal::make_nvp("name", name));
        
        try {
            ar(cereal::make_nvp("endpoint", endpoint));
            if (endpoint.find("@") != std::string::npos && endpoint.find(":") != std::string::npos) {
                std::vector endp1(split(endpoint, '@'));
                std::vector endp2(split(endp1[1], ':'));
                user = endp1[0];
                host = endp2[0];
                port = endp2[1];
            }
            else if (endpoint.find("@") != std::string::npos) {
                std::vector endp1(split(endpoint, '@'));
                user = endp1[0];
                host = endp1[1];
                port = "";
            }
            else if (endpoint.find(":") != std::string::npos) {
                std::vector endp2(split(endpoint, ':'));
                user = "";
                host = endp2[0];
                port = endp2[1];
            }
            else {
                user = "";
                host = endpoint;
                port = "";
            }
        } catch (cereal::Exception&) {
            user = "";
            host = "127.0.0.1";
            port = "";
            ar.setNextName(nullptr);
        }

        try { // If "home_dir" remote path does not exist will be created later in the dff_deploy script
            ar(cereal::make_nvp("home_dir", home_dir));
        } catch (cereal::Exception&) {
            home_dir = default_home_dir;
            ar.setNextName(nullptr);
        }
/*		try { // If "lib_dir" remote path does not exist will be created later in the dff_deploy script
            ar(cereal::make_nvp("lib_dir", lib_dir));
        } catch (cereal::Exception&) {
            lib_dir = default_remote_lib_dir;
            ar.setNextName(nullptr);
        }
*/		lib_dir=home_dir + "/lib";
/*		try { // If "in_dir" remote path does not exist will be created later in the dff_deploy script
            ar(cereal::make_nvp("in_dir", in_dir));
        } catch (cereal::Exception&) {
            in_dir = default_remote_in_dir;
            ar.setNextName(nullptr);
        }
*/		in_dir = home_dir + "/files";

        try {
            ar(cereal::make_nvp("pass_dl", pass_dl));
            if (!pass_dl.compare("yes")) // compare() returns 0 if are the same
                pass_dl = "1";
            else if (!pass_dl.compare("no"))
                pass_dl = "0";
            else if (pass_dl.compare("yes") && pass_dl.compare("no"))
                pass_dl = "1";
        } catch (cereal::Exception&) {
            pass_dl = "1";
            ar.setNextName(nullptr);
        }

        try {
            ar(cereal::make_nvp("rm_dl", rm_dl));
        } catch (cereal::Exception&) {
            rm_dl = "";
            ar.setNextName(nullptr);
        }

        try {
            ar(cereal::make_nvp("rm_files", rm_files));
        } catch (cereal::Exception&) {
            rm_files = "";
            ar.setNextName(nullptr);
        }

        try {
            ar(cereal::make_nvp("my_dl", my_dl)); //TODO ogni cosa che viene scritta viene mandata direttamente al rsync nella certella della libreria, non ci sono controlli che siano effettivamente librerie
        } catch (cereal::Exception&) {
            my_dl = "";
            ar.setNextName(nullptr);
        }

        try {
            ar(cereal::make_nvp("pass_all_files", pass_all_files));
            if (!pass_all_files.compare("yes")) // compare() returns 0 if are the same
                pass_all_files = "1";
            else if (!pass_all_files.compare("no"))
                pass_all_files = "0";
            else if (pass_all_files.compare("yes") && pass_all_files.compare("no"))
                pass_all_files = "0";
        } catch (cereal::Exception&) {
            pass_all_files = "0";
            ar.setNextName(nullptr);
        }

        try {
            ar(cereal::make_nvp("files", files));
        } catch (cereal::Exception&) {
            files = "";
            ar.setNextName(nullptr);
        }
    }

    void run(){
        char command[4096];
        char c_shared_fs[1024];
        char c_local[1024];
        char c_remote[1024];
        char c_deploy[1024];

        std::string deploy_files("");
        if (!pass_all_files.compare("1"))
            deploy_files = all_files;
        else {
            std::vector tmp_yes(split(files, ' '));
            for (auto elem : tmp_yes) {
                if ( cmd_files.find(elem) != cmd_files.end())
                    deploy_files += std::string(cmd_files[elem]) + " ";
            }
        }

        sprintf (c_deploy, "DFF_DRY=%d DFF_dl=%s DFF_rm_dl=\"%s\" DFF_my_dl=\"%s\" DFF_rm_files=\"%s\" DFF_ssh=%s DFF_lib='%s' DFF_home='%s' DFF_io='%s' %s/dff_deploy.sh %s%s%s %s %s %s; ",
            dry_run,
            pass_dl.c_str(),
            rm_dl.c_str(),
            my_dl.c_str(),
            rm_files.c_str(),
            ssh_key_dir.c_str(),
            lib_dir.c_str(), // Use quotes to inhibit initial expansion
            home_dir.c_str(), // Use quotes to inhibit initial expansion
            in_dir.c_str(), // Use quotes to inhibit initial expansion
            argv_zero.substr(0, argv_zero.find_last_of("/\\") + 1).c_str(), // Working directory contains dff_run and dff_deploy.sh
            user.c_str(),
            user.compare("") ? "@" : "",
            host.c_str(),
            executable.c_str(),
            configFile.c_str(),
            deploy_files.c_str()
        );

        sprintf(c_remote, "ssh -T -i %s/ff_key %s%s%s 'DFF_home=%s &>/dev/null; cd %s; LD_LIBRARY_PATH=\"%s:${LD_LIBRARY_PATH}\" %s/%s %s --DFF_Config=%s/%s --DFF_GName=%s %s 2>&1;';",
            ssh_key_dir.c_str(),
            user.c_str(),
            user.compare("") ? "@" : "",
            host.c_str(),
            home_dir.c_str(),
            home_dir.c_str(),
            lib_dir.c_str(),
            home_dir.c_str(),
            executable.substr(executable.find_last_of("/\\") + 1).c_str(), // Extract the name of the file being executed
            executable_remote_param.c_str(),
            home_dir.c_str(),
            configFile.substr(configFile.find_last_of("/\\") + 1).c_str(), // Extract the name of the configuration file was parsed
            this->name.c_str(),
            toBePrinted(this->name) ? "" : "> /dev/null"
        );

        sprintf(c_local, "%s %s --DFF_Config=%s --DFF_GName=%s %s 2>&1;",
            executable.c_str(),
            executable_param.c_str(),
            configFile.c_str(),
            this->name.c_str(),
            toBePrinted(this->name) ? "" : "> /dev/null"
        );

        sprintf(c_shared_fs, " %s %s %s %s %s --DFF_Config=%s --DFF_GName=%s %s 2>&1 %s",
            (isRemote() ? "ssh -T " : ""),
            (isRemote() ? host.c_str() : ""),
            (isRemote() ? "'" : ""),
            executable.c_str(),
            executable_param.c_str(),
            configFile.c_str(),
            this->name.c_str(),
            toBePrinted(this->name) ? "" : "> /dev/null",
            (isRemote() ? "'" : "")
        );

        if (shared_fs)
            sprintf(command,"%s",
                c_shared_fs
            );
        else
            sprintf(command,"%s %s %s",
                isRemote() ? c_deploy : "",
                isRemote() ? "deploy_out=$?; if ! [ $deploy_out = 0 ]; then echo 'Failed: Problem with dff_deploy.sh. Stopped execution before exec.'; exit $deploy_out; fi;" : "",
                isRemote() ? c_remote : c_local
            );

        if (dry_run){
            if (shared_fs) {
                std::cout << "On " << user.c_str() << (user.compare("") ? "@" : "") << host.c_str() << std::endl;
                if (isRemote()) // Show the command sent remotely (the substring between quotes in c_shared_fs)
                    fprintf(stdout, "(ssh)\n  %s\n\n", getRemoteCmd(c_shared_fs).c_str());
                else
                    std::cout << c_shared_fs << "\n" << std::endl;
            }
            else {
                if (isRemote()) { // Execute pre-command "dff_deploy.sh" and show the command sent remotely (the substring between quotes in c_remote)
                    if (option_V) std::cout << "Pre-command: " << c_deploy << std::endl;
                    if (system(c_deploy) != 0)
                        std::cout << "Deploy command failed" << std::endl;
                    std::cout << "On " << user.c_str() << (user.compare("") ? "@" : "") << host.c_str() << std::endl;
                    fprintf(stdout, "(ssh)\n  %s\n\n", getRemoteCmd(c_remote).c_str());
                }
                else {
                    std::cout << "On " << user.c_str() << (user.compare("") ? "@" : "") << host.c_str() << std::endl;
                    std::cout << c_local << "\n" << std::endl;
                }
            }
        }
        else {
            // Show and execute the entire 'command'
            std::cout << "Executing the following command:\n" << (isRemote() ? "      " : "" ) << command << std::endl;
            file = popen(command, "r");
            fd = fileno(file);
            if (fd == -1) {
                fprintf(stderr, "Failed to run command\n" );
                exit(1);
            }

            int flags = fcntl(fd, F_GETFL, 0);
            flags |= O_NONBLOCK;
            fcntl(fd, F_SETFL, flags);
        }
    }

    bool isRemote(){
        if (!host.compare("127.0.0.1") || !host.compare("localhost") || !host.compare(hostname))
            return false;
        
        char ip1[INET_ADDRSTRLEN];
        char ip2[INET_ADDRSTRLEN];
        convertToIP(host.c_str(), ip1);
        convertToIP(hostname, ip2); // To have an effective translation remember to setup file "/etc/hosts" correctly
        if (strncmp(ip1,ip2,INET_ADDRSTRLEN)==0) return false;
        return true; // Is remote
    }

    std::string getRemoteCmd(std::string cmd){
        std::string tmp;
        std::size_t posA, posB;

        if (cmd.find_first_of("'\\") == std::string::npos)
            return cmd;
        posA = cmd.find_first_of("'\\")+1;
        posB = cmd.find_last_of("'\\");
        if (posA-1 == posB)
            return cmd;
        return cmd.substr(posA,posB-posA);
    }
};

bool allTerminated(std::vector<G>& groups){
    for (G& g: groups)
        if (g.file != nullptr)
            return false;
    return true;
}

static inline void usage(char* progname) {
    std::cout << "\nUSAGE: " <<  progname << " [Options] -f <configFile> <cmd>\n"
              << "Options: \n"
              << "\t -v <g1>,...,<g2> \t Prints the output of the specified groups\n"
              << "\t -V               \t Print the output of all groups\n"
              << "\t -p \"TCP|MPI\"   \t Force communication protocol\n"
              << "\t --dry-run        \t Rehearsal of a performance or procedure before the real one\n";
    std::cout << "\n";
}

std::string generateRankFile(std::vector<G>& parsedGroups, std::string dir){ // Used in MPI protocol
    std::string name = std::string(dir) + "/dffRankfile" + std::to_string(getpid());

    std::ofstream tmpFile(name, std::ofstream::out);
    
    for(size_t i = 0; i < parsedGroups.size(); i++)
        tmpFile << "rank " << i << "=" << parsedGroups[i].host << " slot=0:*\n";  // TODO: to use the "threadMapping" attribute
    /*for (const G& group : parsedGroups)
        tmpFile << group.host << std::endl;*/

    tmpFile.close();
    // Return the name of the temporary file just created; remember to remove it after the usage
    return name;
}

int main(int argc, char** argv) {

    if (argc == 1 ||
        strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "-h") == 0){
        usage(argv[0]);
        exit(EXIT_SUCCESS);
    }

    // Get the hostname
    if (gethostname(hostname, HOST_NAME_MAX) != 0) {
        perror("gethostname");
        exit(EXIT_FAILURE);
    }

    int optind=0; // Index of the first parameter after program options
    for(int i=1;i<argc;++i) {
        if (argv[i][0]=='-') {
            switch(argv[i][1]) {
            case 'p' : {
                if (argv[i+1] == NULL) {
                    std::cerr << "-p require a protocol\n";
                    usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                std::string forcedProtocol = std::string(argv[++i]);
                if (forcedProtocol == "MPI")      protocol = Proto::MPI;
                else if (forcedProtocol == "TCP") protocol = Proto::TCP;
                else {
                    std::cerr << "-p require a valid protocol (TCP or MPI)\n";
                    exit(EXIT_FAILURE);
                }
            } break;

            case 'f': {
                if (argv[i+1] == NULL) {
                    std::cerr << "-f requires a file name\n";
                    usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                configFile = n_fs::absolute(n_fs::path(argv[++i])).string();
            } break;

            case 'V': {
                option_V=true;
            } break;

            case 'v': {
                if (argv[i+1] == NULL) {
                    std::cerr << "-v requires at list one argument\n";
                    usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                viewGroups = split(argv[i+1], ',');
                i+=viewGroups.size();
            } break;

            case '-': {
                if (argv[i] == std::string("--dry-run"))
                    dry_run=true;
                else {
                    std::cerr << "'" << argv[i] << "' is not recognized as a valid option\n";
                    usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
            } break;

            default:
                std::cerr << "'" << argv[i] << "' is not recognized as a valid option\n";
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        } else { optind=i; break;}
    }

    if (configFile == "") {
        std::cerr << "ERROR: Missing config file for the loader\n";
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    argv_zero = n_fs::absolute(n_fs::path(argv[0])).string();
    executable = n_fs::absolute(n_fs::path(argv[optind])).string();
    if (!n_fs::exists(executable) || access(executable.c_str(),X_OK)==-1 ) {
        std::cerr << "ERROR: Unable to find or exec the executable file (we found as executable '" << argv[optind] << "')\n";
        exit(EXIT_FAILURE);
    }    
    std::ifstream is(configFile);
    if (!is){
        std::cerr << "Unable to open configuration file for the program!" << std::endl;
        return -1;
    }

    std::vector<G> parsedGroups;
    try {
        cereal::JSONInputArchive ar(is);
        try {
            std::string tmpShared; //TODO qua sotto sembra che se non e definito genera problemi...
            ar(cereal::make_nvp("shared_filesystem", tmpShared)); // Get parameter "shared_filesystem" from the json file
            if (!tmpShared.compare("yes")) // compare() returns 0 if are the same
                shared_fs = true;
            else if (!tmpShared.compare("no"))
                shared_fs = false;
            else if (tmpShared.compare("no") && tmpShared.compare("yes"))
                shared_fs = true;
        } catch (cereal::Exception& e){
            ar.setNextName(nullptr);
            shared_fs = true;
        }

        if (!protocol) // Get the protocol to be used from the configuration file if it was not forced by the command line
            try {
                std::string tmpProtocol;
                ar(cereal::make_nvp("protocol", tmpProtocol)); // Get parameter "protocol" from the json file
                if (tmpProtocol == "MPI")
                    protocol = Proto::MPI;
                else 
                    protocol = Proto::TCP;
            } catch (cereal::Exception&) {
                ar.setNextName(nullptr);
                // If the protocol is not specified we assume TCP
                protocol = Proto::TCP;
            }

        if (!ssh_key_dir.compare(""))
            try { // If "ssh_key_dir" local path does not exist will be created later in the dff_deploy script
                ar(cereal::make_nvp("ssh_key_dir", ssh_key_dir)); // Get parameter "ssh_key_dir" from the json file
            } catch (cereal::Exception&) {
                ar.setNextName(nullptr);
                ssh_key_dir = "$HOME/.ssh/";
            }

        if (!default_home_dir.compare(""))
            try { // If "default_home_dir" remote path does not exist will be created later in the dff_deploy script
                ar(cereal::make_nvp("default_home_dir", default_home_dir)); // Get parameter "default_home_dir" from the json file
            } catch (cereal::Exception&) {
                ar.setNextName(nullptr);
                if(protocol == Proto::TCP)
                    default_home_dir = "$HOME/opt/fastflow/";
                if(protocol == Proto::MPI)
                    default_home_dir = "/tmp/fastflow/";
            }

        // Parse all the groups in the configuration file
        ar(cereal::make_nvp("groups", parsedGroups));
    } catch (const cereal::Exception& e){
        std::cerr << "Error parsing the JSON config file. Check syntax and structure of the file and retry!" << std::endl;
        exit(EXIT_FAILURE);
    }

    all_files = "";
    executable_param = ""; // Parameters related to the local machine
    executable_remote_param = ""; // Parameters related to the remote machine
    std::ifstream file;
    std::string tmp;
    for (int index = optind+1 ; index < argc; index++) {
        executable_param += std::string(argv[index]) + " ";
        tmp = std::string(n_fs::absolute(n_fs::path(argv[index])));
        file.open(tmp);
        if (file) {
            all_files += std::string(tmp) + " ";
            if (n_fs::is_directory(tmp)){
                std::string temp= tmp.substr(0,tmp.length()-1);
                cmd_files[ temp.substr(temp.find_last_of("/\\")+1).c_str() ] = tmp;
                executable_remote_param += "$DFF_home/files" + std::string(temp.substr(temp.find_last_of("/\\"))) + " ";
            }
            else {
                cmd_files[ tmp.substr(tmp.find_last_of("/\\")+1).c_str() ] = tmp;
                executable_remote_param += "$DFF_home/files" + std::string(tmp.substr(tmp.find_last_of("/\\"))) + " ";
            }
        }
        else {
            executable_remote_param += std::string(std::string(argv[index]).substr(std::string(argv[index]).find_last_of("/\\")+1)) + " ";
        }
        file.close();
    }

    #ifdef DEBUG
        for(auto& g : parsedGroups)
            std::cout << "Group: " << g.name << " on host " << g.host << std::endl;
    #endif

    if (protocol == Proto::TCP){
        auto Tstart = getusec(); // Start the timer
        for (G& g : parsedGroups)
            g.run();
        
        while(!allTerminated(parsedGroups)){
            for(G& g : parsedGroups){
                if (g.file != nullptr){
                    char buff[1024] = { 0 };
                    
                    ssize_t result = read(g.fd, buff, sizeof(buff));
                    if (result == -1){
                        if (errno == EAGAIN)
                            continue;

                        int code = pclose(g.file);
                        if (WEXITSTATUS(code) != 0)
                            std::cout << "[" << g.name << "][ERR] Report an return code: " << WEXITSTATUS(code) << std::endl;
                        g.file = nullptr;
                    } else if (result > 0){
                        std::cout << buff;
                    } else {
                        int code = pclose(g.file);
                        if (WEXITSTATUS(code) != 0)
                            std::cout << "[" << g.name << "][ERR] Report an return code: " << WEXITSTATUS(code) << std::endl;
                        g.file = nullptr;
                    }
                }
            }

        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        std::cout << "Elapsed time: " << (getusec()-(Tstart))/1000 << " ms" << std::endl; // Stop the timer
    }

    if (protocol == Proto::MPI){
        std::string rankFile;
        if(shared_fs) // Created rankfile to use in mpirun
            rankFile = generateRankFile(parsedGroups, "/tmp");
        else
            rankFile = generateRankFile(parsedGroups, default_home_dir);
        std::cout << "RankFile: " << rankFile << std::endl;

        char command[2048];
        char c_deploy[1024];
        char c_move[1024];

        if(shared_fs) {
            sprintf(command, "mpirun -np %lu --rankfile %s %s %s --DFF_Config=%s;",
                parsedGroups.size(),
                rankFile.c_str(),
                executable.c_str(),
                executable_param.c_str(),
                configFile.c_str()
            );
        }
        else { //TODO funziona quando la macchina da cui lo lancio e' nel pool di quelle che eseguono anche (testare)?
//			std::string id = "MPI_" + rankFile.substr(rankFile.find_last_not_of("0123456789") + 1);    // Use the PID extracted from the end of rankfile to create a unique reference
//			std::string id = "MPI_" + executable.substr(executable.find_last_of("/\\") + 1);           // Use the name of the executable to create a reference

            std::string lib_dir = default_home_dir + "/lib";
            std::string in_dir = default_home_dir + "/files";
            std::string deploy_files("");
            for (G& g : parsedGroups){
                if (!g.pass_all_files.compare("1"))
                    deploy_files = all_files;
                else {
                    std::vector tmp_yes(split(g.files, ' '));
                    for (auto elem : tmp_yes) {
                        if ( cmd_files.find(elem) != cmd_files.end())
                            deploy_files += std::string(cmd_files[elem]) + " ";
                    }
                }
                if(g.isRemote()){
                    sprintf (c_deploy, "DFF_DRY=%d DFF_dl=%s DFF_rm_dl=\"%s\" DFF_my_dl=\"%s\" DFF_rm_files=\"%s\" DFF_ssh=%s DFF_lib='%s' DFF_home='%s' DFF_io='%s' %s/dff_deploy.sh %s%s%s %s %s %s; ",
                        dry_run,
                        g.pass_dl.c_str(),
                        g.rm_dl.c_str(),
                        g.my_dl.c_str(),
                        g.rm_files.c_str(),
                        ssh_key_dir.c_str(),
                        lib_dir.c_str(), // Use quotes to inhibit initial expansion
                        default_home_dir.c_str(), // Use quotes to inhibit initial expansion
                        in_dir.c_str(), // Use quotes to inhibit initial expansion
                        argv_zero.substr(0, argv_zero.find_last_of("/\\") + 1).c_str(), // Working directory contains dff_run and dff_deploy.sh
                        g.user.c_str(),
                        g.user.compare("") ? "@" : "",
                        g.host.c_str(),
                        executable.c_str(),
                        configFile.c_str(),
                        deploy_files.c_str()
                    );

                    std::cout << "Deploying on " << g.host.c_str() << std::endl;
                    if (option_V) std::cout << "Pre-command: " << c_deploy << std::endl;
                    if (system(c_deploy) != 0)
                        std::cout << "Deploy command failed" << std::endl;
                }
                else {
                    sprintf (c_move, "mkdir -p %s &>/dev/null; cp -fr -t %s -- %s %s &>/dev/null; mkdir -p %s &>/dev/null; cp -fr -t %s -- %s &>/dev/null;",
                    default_home_dir.c_str(),
                    default_home_dir.c_str(),
                    executable.c_str(),
                    configFile.c_str(),
                    in_dir.c_str(),
                    in_dir.c_str(),
                    executable_param.c_str()
                    );

                    if (dry_run){
                        std::cout << "Copying " << executable << " and " << configFile << " to " << default_home_dir<< std::endl;
                        std::cout << "Copying additional files" << " to " << in_dir<< std::endl;
                    }
                    else {
                        if (option_V) std::cout << "Pre-command: " << c_move << std::endl;
                        if (system(c_move) != 0)
                            std::cout << "Local deploy command failed" << std::endl;
                    }
                }
            }
//TODO cosa serve le cose remote se lo lancio sempre in locale?
//faccio i c_move dei file anche della macchina cerrente anche se non e' nell'esecuzione?
            sprintf(command, "DFF_home=%s &>/dev/null; cd %s &>/dev/null; LD_LIBRARY_PATH=\"%s:$LD_LIBRARY_PATH\"; mpirun -x LD_LIBRARY_PATH -np %lu --rankfile %s %s/%s %s --DFF_Config=%s/%s;",
                default_home_dir.c_str(),
                default_home_dir.c_str(),
                lib_dir.c_str(),
                parsedGroups.size(),
                rankFile.c_str(),
                default_home_dir.c_str(),
                executable.substr(executable.find_last_of("/\\") + 1).c_str(), // Extract the name of the file being executed
                executable_remote_param.c_str(),
                default_home_dir.c_str(),
                configFile.substr(configFile.find_last_of("/\\") + 1).c_str() // Extract the name of the configuration file was parsed
            );
        }

        if (dry_run){
            std::cout << "Command:  " << command << std::endl;
            if (!option_V)
                std::remove(rankFile.c_str());
            return 0;
        }
        std::cout << "\nExecuting the following command:\n" << "      " << command << std::endl;
        FILE *fp;
        char buff[1024];
        fp = popen(command, "r");
        if (fp == NULL) {
            printf("Failed to run command\n" );
            exit(1);
        }

        /* Read the output a line at a time - output it. */
        while (fgets(buff, sizeof(buff), fp) != NULL) {
            std::cout << buff;
        }

        pclose(fp);

        if (!option_V)
            std::remove(rankFile.c_str());
    }
    return 0;
}
