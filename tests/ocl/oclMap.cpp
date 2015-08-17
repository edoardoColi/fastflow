/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* ***************************************************************************
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as 
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  As a special exception, you may use this file as part of a free software
 *  library without restriction.  Specifically, if other files instantiate
 *  templates or use macros or inline functions from this file, or you compile
 *  this file and link it with other files to produce an executable, this
 *  file does not by itself cause the resulting executable to be covered by
 *  the GNU General Public License.  This exception does not however
 *  invalidate any other reasons why the executable file might be covered by
 *  the GNU General Public License.
 *
 ****************************************************************************
 */
/* Author: Massimo Torquati
 *         torquati@di.unipi.it  massimotor@gmail.com
 */

#if !defined(FF_OPENCL)
#define FF_OPENCL
#endif

#include <ff/stencilReduceOCL.hpp>

using namespace ff;

#define CHECK 1
#ifdef CHECK
#include "ctest.h"
#else
#define NACC 1
#endif


//obsolete
//FF_OCL_MAP_ELEMFUNC(mapf, float, elem, useless,
//                    (void)useless;
//                    return (elem+1.0);
//                    );

FF_OCL_MAP_ELEMFUNC_1D(mapf, float, elem,
		return elem+1.0;
);


struct oclTask: public baseOCLTask<oclTask, float> {
    oclTask():M(NULL),size(0) {}
    oclTask(float *M, size_t size):M(M),size(size) {}
    void setTask(const oclTask *t) { 
        assert(t);
        setInPtr(t->M, t->size);
        setOutPtr(t->M);
    }

    float *M;
    const size_t  size;
};

int main(int argc, char * argv[]) {
    size_t size=2048;
    if(argc>1) size     =atol(argv[1]);
    printf("arraysize = %ld\n", size);

    float *M        = new float[size];
    for(size_t j=0;j<size;++j) M[j]=j;

#ifdef CHECK
    float *M_        = new float[size];
    memcpy(M_, M, size * sizeof(float));
#endif

    oclTask oclt(M, size);
    ff_mapOCL_1D<oclTask> oclMap(oclt, mapf, nullptr, NACC);
    SET_DEVICE_TYPE(oclMap);
    
    oclMap.run_and_wait_end();


#if defined(CHECK)
	for (size_t i = 0; i < size; ++i) {
		if ((M_[i]+1) != M[i]) {
            printf("Error\n");
            exit(1);
        }
	}
    printf("Result correct\n");
    delete [] M_;
#endif

    delete [] M;
    return 0;
}
