OVIS_DIR=[INSERT PATH TO OVIS INSTALL]
KOKKOS_DIR=[INSERT PATH TO KOKKOS TOOLS INSTALL]/kokkos-tools/
CXX=g++
CXXFLAGS=-O3 -std=c++11 -g \
	-I$(OVIS_DIR)/include/ -I./include -I$(KOKKOS_DIR)/profiling/all/ -I$(KOKKOS_DIR)/common/makefile-only/

SHARED_CXXFLAGS=-shared -fPIC
LDFLAGS=-L$(OVIS_DIR)/lib
LIBS=-lldmsd_stream -lldms -lrt

all: kp_kernel_ldms.so
MAKEFILE_PATH := $(subst Makefile,,$(abspath $(lastword $(MAKEFILE_LIST))))

CXXFLAGS+=-I${MAKEFILE_PATH}

kp_kernel_ldms.so: ${MAKEFILE_PATH}kp_kernel_ldms.cpp ${MAKEFILE_PATH}kp_kernel_info.h
	$(CXX) $(SHARED_CXXFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ ${MAKEFILE_PATH}kp_kernel_ldms.cpp \
	$(LIBS)
clean:
	rm *.so
