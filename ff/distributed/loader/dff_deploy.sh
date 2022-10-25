#!/bin/bash

usage()
{
	echo "Usage: [Options] DFF_* ./$(basename "$0") [user@]hostname [<executable> <JSONconfig> [<file_needed>...]]"
	echo "DFF_*:"
	echo -e "\t DFF_ssh  = <ssh_key_dir>"
	echo -e "\t DFF_lib  = <remote_lib_dir>"
	echo -e "\t DFF_home = <remote_home_dir>"
	echo -e "\t DFF_io   = <remote_iofiles_dir>"
	echo "Options:"
	echo -e "\t DFF_PAIR     \t(Bool Value)   Establishes ssh connection with the remote node"
	echo -e "\t DFF_DRY      \t(Bool Value)   Rehearsal of a performance or procedure before the real one"
	echo -e "\t DFF_dl       \t(Bool Value)   Share Dinamic Libraries"
	echo -e "\t DFF_rm_dl    \t(String Value) Dinamic Libraries to ignore"
	echo -e "\t DFF_my_dl    \t(String Value) Share specified Dinamic Libraries"
	echo -e "\t DFF_files    \t(Bool Value)   Share additional files"
	echo -e "\t DFF_rm_files \t(String Value) Additional files to ignore"
	echo -e "\t DFF_CHECK    \t(Bool Value)   Check that files and their respective paths are correct"
	echo -e "\t DFF_LOG      \t(String Value) Output log file"
	exit 1
}

if [ -z ${DFF_DRY+x} ]; then
DFF_DRY=0																										# Rehearsal of a performance or procedure before the real one
fi
if [ -z ${DFF_PAIR+x} ]; then
DFF_PAIR=1																										# Generate ssk-pair key and ask for the password
fi
if [ -z ${CHECK+x} ]; then
CHECK=1																											# Checks the integrity of the inserted parameters
fi
if [ -z ${DFF_dl+x} ]; then
DFF_dl=1																										# Dinamic Libraries are passed
fi
if [ -z ${DFF_rm_dl+x} ]; then
DFF_rm_dl=""																									# List of dinamic library to not sync
fi
if [ -z ${DFF_my_dl+x} ]; then
DFF_my_dl=""																									# List of specified dinamic library to share
fi
if [ -z ${DFF_files+x} ]; then
DFF_files=1																										# Additional files are passed
fi
if [ -z ${DFF_rm_files+x} ]; then
DFF_rm_files=""																									# List of dinamic library to not sync
fi
if [ -z ${LOG+x} ]; then
LOG="/tmp/dff_deployment.log"																					# Log file for deployment script
echo "====================================================================================================">>$LOG
date>>$LOG
echo "====================================================================================================">>$LOG
fi
if [ -z ${DFF_ssh+x} ] || [ -z ${DFF_lib+x} ] || [ -z ${DFF_home+x} ] | [ -z ${DFF_io+x} ]; then
usage
fi

check_dir()																										# Verify that the specified $1 directory exists starting from directory $2
{
echo "Try executing          cd "$2"">>$LOG
cd "$2" &>>$LOG
if [ ! -d $1 ]; then
	echo "$1 is not an existing directory"
	echo "EXIT STATUS: 1" >>$LOG
	echo "EXIT STATUS: 1"
	echo -e "Check logfile for more: $LOG\n"
	exit 1;
fi
}

check_file()																									# Verify that the file is valid
{
if [ ! -f "$1" ]; then
	echo "$(basename "$1") is not a valid file"
	echo "EXIT STATUS: 1" >>$LOG
	echo "EXIT STATUS: 1"
	echo -e "Check logfile for more: $LOG\n"
	exit 1;
fi
}

check_exec()																									# Verify that the file is executable
{
if [ ! -x "$1" ]; then
	echo "$(basename "$1") is not executable"
	echo "EXIT STATUS: 2" >>$LOG
	echo "EXIT STATUS: 2"
	echo -e "Check logfile for more: $LOG\n"
	exit 2;
fi
}

num_args=$#
working_dir=$(pwd)
if [ $num_args != 1 ] && [ $num_args != 3 ] && [ $num_args -lt 4 ]; then
	usage
fi

#	hostname=$1
#	if [ $CHECK = 1 ]; then
#		if ! [[ "${hostname##*@}" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then					# Verify that the hostname is of the type xxx.xxx.xxx.xxx
#			echo "Invalid hostname: ${hostname##*@}"
#			echo "EXIT STATUS: 42" >>$LOG
#			exit 42;
#		fi
#	fi

### Ssh Pairing ###
if [ ! -d $DFF_ssh ]; then																						# Create the folder for the local ssh key if it doesn't exist
	if [ $DFF_PAIR = 1 ]; then
		mkdir -p $DFF_ssh
		chmod 700 $DFF_ssh
	else
		echo "Make directory $DFF_ssh"
	fi
fi
echo "Try executing          cd $DFF_ssh">>$LOG
cd $DFF_ssh	&>>$LOG
abs_path_ssh=$(pwd)
if ! [ -f ff_key ] || ! [ -f ff_key.pub ]; then																	# If there are no keys, it generates them
	if [ $DFF_PAIR = 1 ]; then
		rm -f ff_key ff_key.pub
		echo "Try executing          ssh-keygen -f ff_key -t rsa -N """>>$LOG
		ssh-keygen -f ff_key -t rsa -N ""	&>>$LOG
	else
		echo "Generate ssh keys" #TODO si se arg=1 e dryrun=1 (caso escluso dall'utilizzo in fastflow)
	fi
fi
if [ $DFF_PAIR = 1 ]; then
	echo "Try executing          ssh-copy-id -i "$abs_path_ssh"/ff_key.pub "$1"">>$LOG
	ssh-copy-id -i "$abs_path_ssh"/ff_key.pub "$1"	&>>$LOG														# Share the public key for direct access
else
	echo "Use ssh-copy-id to share public key in $abs_path_ssh over $1"
fi
if [ $? = 1 ]; then
	echo "EXIT STATUS: 3" >>$LOG
	echo "EXIT STATUS: 3"
	echo -e "Check logfile for more: $LOG\n"
	exit 3;
fi
if [ $num_args = 1 ]; then
	if [ $DFF_DRY = 0 ]; then
		echo "Achieve direct network for $1"
	fi
	echo "EXIT STATUS: 0" >>$LOG
	exit 0;
fi

if [ $CHECK = 1 ]; then
	check_dir $(dirname "$2") $working_dir																		# Check the directory of executable
	check_dir $(dirname "$3") $working_dir																		# Check the directory of JSONconfig
fi

cd "$working_dir"
echo "Try executing          cd $(dirname "$2")">>$LOG
cd $(dirname "$2") &>>$LOG
if [ $? = 1 ]; then
	echo "EXIT STATUS: 1" >>$LOG
	echo "EXIT STATUS: 1"
	echo -e "Check logfile for more: $LOG\n"
	exit 1;
fi
abs_path_exec=$(pwd)/$(basename "$2")

cd "$working_dir"
echo "Try executing          cd $(dirname "$3")">>$LOG
cd $(dirname "$3") &>>$LOG
if [ $? = 1 ]; then
	echo "EXIT STATUS: 1" >>$LOG
	echo "EXIT STATUS: 1"
	echo -e "Check logfile for more: $LOG\n"
	exit 1;
fi
abs_path_JSON=$(pwd)/$(basename "$3")

if [ $CHECK = 1 ]; then																							# Check that all passed files are files
	check_file $abs_path_exec
	check_exec $abs_path_exec																					# Check if is executable
	check_file $abs_path_JSON
	iterator=0
	for file in "$@"																							# Iterate over files needed
	do
		iterator=$((iterator+1))
		if [ $iterator -ge 4 ]; then
			cd $working_dir
			if [ ! -d $file ]; then
				echo "Try executing          cd $(dirname "$file")">>$LOG
				cd $(dirname "$file") &>>$LOG
				if [ $? = 1 ]; then
					echo "EXIT STATUS: 1" >>$LOG
					echo "EXIT STATUS: 1"
					echo -e "Check logfile for more: $LOG\n"
					exit 1;
				fi
				if [ ! -f $(basename "$file") ]; then
					echo "$file is not a valid file or directory"
					echo "EXIT STATUS: 1" >>$LOG
					echo "EXIT STATUS: 1"
					echo -e "Check logfile for more: $LOG\n"
					exit 1;
				fi
			fi
		fi
	done
fi

echo "Try executing          ldd "$abs_path_exec"">>$LOG
ldd "$abs_path_exec" &>>$LOG																					# An executable with dynamic libraries was not specified
if [ $? != 0 ]; then
	echo "Problem about $2: Not a dynamic executable"
	echo
	usage
fi

if [ $DFF_dl = 1 ]; then
DFF_rm_dl=$(echo $DFF_rm_dl | awk 'BEGIN{FS=OFS=" "} {for (i=1;i<=NF;i++) {$i="--exclude="$i""}} 1')
	if [ $DFF_DRY = 0 ]; then
		printf '%-40s' "Passing shared dynamic libraries..."													# Can also be used "echo Passing shared dynamic libraries..."
		ssh -i "$abs_path_ssh"/ff_key "$1" "mkdir -p $DFF_lib"													# Check the dependencies of the executables and move them to the specified node
		if [ $? != 0 ]; then
			echo "ERROR"
			echo "EXIT STATUS: 4" >>$LOG
			echo "EXIT STATUS: 4"
			echo -e "Check logfile for more: $LOG\n"
			exit 4
		fi

		echo "Try executing          ???(Passing Shared Objects)">>$LOG #TODO
		ldd "$abs_path_exec" | grep "=> /" | awk '{print $3}' | xargs -I '{}' rsync -rvLE -e "ssh -i "$abs_path_ssh"/ff_key" $DFF_rm_dl '{}' "$1":"$DFF_lib" &>>$LOG
		echo "DONE"
	else
		echo "      If not exist create '$DFF_lib' remote directory"
#		echo "Check shared object dependencies of /bin/bash"
		echo "Check shared object dependencies of $abs_path_exec"
		printf 'Rsync %-100s to %s\n' "shared objects" "$1:$DFF_lib"											# Can also be used "echo Rsync shared objects to \"$1:$DFF_lib\" "
	fi
fi
if [ ! -z $DFF_my_dl ]; then
	ssh -i "$abs_path_ssh"/ff_key "$1" "mkdir -p $DFF_lib"
	echo "Try executing          rsync -vL -e "ssh -i "$abs_path_ssh"/ff_key" $DFF_my_dl "$1":"$DFF_lib"">>$LOG
	rsync -vL -e "ssh -i "$abs_path_ssh"/ff_key" $DFF_my_dl "$1":"$DFF_lib" &>>$LOG
fi

if [ $DFF_DRY = 0 ]; then
	printf '%-40s'  "Passing shared files..."																	# Can also be used "echo ..."
	echo "Try executing          ssh -i "$abs_path_ssh"/ff_key "$1" "mkdir -p $DFF_home"">>$LOG
	ssh -i "$abs_path_ssh"/ff_key "$1" "mkdir -p $DFF_home" &>>$LOG
	if [ $? != 0 ]; then
		echo "ERROR"
		echo "EXIT STATUS: 4" >>$LOG
		echo "EXIT STATUS: 4"
		echo -e "Check logfile for more: $LOG\n"
		exit 4
	fi
	echo "Try executing          rsync -vL -e "ssh -i "$abs_path_ssh"/ff_key" $abs_path_exec "$1":"$DFF_home"">>$LOG
	rsync -vL -e "ssh -i "$abs_path_ssh"/ff_key" $abs_path_exec "$1":"$DFF_home" &>>$LOG
	echo "Try executing          rsync -vL -e "ssh -i "$abs_path_ssh"/ff_key" $abs_path_JSON "$1":"$DFF_home"">>$LOG
	rsync -vL -e "ssh -i "$abs_path_ssh"/ff_key" $abs_path_JSON "$1":"$DFF_home" &>>$LOG
else
	echo "      If not exist create '$DFF_home' remote directory"
	printf 'Rsync %-100s to %s\n' "$abs_path_exec" "$1:$DFF_home"												# Can also be used "echo ..."
	printf 'Rsync %-100s to %s\n' "$abs_path_JSON" "$1:$DFF_home"												# Can also be used "echo ..."
fi

if [ $DFF_files = 1 ]; then
DFF_rm_files=$(echo $DFF_rm_files | awk 'BEGIN{FS=OFS=" "} {for (i=1;i<=NF;i++) {$i="--exclude="$i""}} 1')
	if [ $DFF_DRY = 1 ]; then
		echo "      If not exist create '$DFF_io' remote directory"
	fi
	iterator=0
	for file in "$@"																							# Move necessary files to the specified node for execution
	do
		iterator=$((iterator+1))
		if [ $iterator -ge 4 ]; then
			cd "$working_dir"
			echo "Try executing          cd $(dirname "$file")">>$LOG
			cd $(dirname "$file") &>>$LOG
			if [ $DFF_DRY = 0 ]; then
				echo "Try executing          ssh -i "$abs_path_ssh"/ff_key "$1" "mkdir -p $DFF_io"">>$LOG
				ssh -i "$abs_path_ssh"/ff_key "$1" "mkdir -p $DFF_io" &>>$LOG
				if [ $? != 0 ]; then
					echo "ERROR"
					echo "EXIT STATUS: 4" >>$LOG
					echo "EXIT STATUS: 4"
					echo -e "Check logfile for more: $LOG\n"
					exit 4
				fi
				echo "Try executing          rsync -rvL -e "ssh -i "$abs_path_ssh"/ff_key" $DFF_rm_files $(pwd)/$(basename "$file") "$1":"$DFF_io"">>$LOG
				rsync -rvL -e "ssh -i "$abs_path_ssh"/ff_key" $DFF_rm_files $(pwd)/$(basename "$file") "$1":"$DFF_io" &>>$LOG
			else
				if [ -d $(pwd)/$(basename "$file")/ ]; then
					printf 'Rsync %-100s to %s\n' "$(pwd)/$(basename "$file")"/ "$1:$DFF_io"					# Can also be used "echo ..."
				else
					printf 'Rsync %-100s to %s\n' "$(pwd)/$(basename "$file")" "$1:$DFF_io"						# Can also be used "echo ..."
				fi
			fi
		fi
	done
	if [ $DFF_DRY = 0 ]; then
		echo "DONE"
	fi
fi

echo "EXIT STATUS: 0" >>$LOG
exit 0
