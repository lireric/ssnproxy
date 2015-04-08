
include Makefile.common

RM := rm -rf

LDFLAGS := $(CFLAGS) -L/usr/local/lib
SUBDIRS := src
LIBS := -lboost_program_options -lpthread -lboost_date_time -lboost_system -lboost_thread -lboost_chrono

CPP_SRCS += ../src/AsyncSerial.cpp ../src/ssnproxy.cpp 
C_SRCS += ../src/crc.c 

OBJS += ./src/ssnproxy.o ./src/AsyncSerial.o ./src/crc.o 

CPP_DEPS += ./src/AsyncSerial.d ./src/ssnproxy.d 
C_DEPS += ./src/crc.d 

all: src
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C++ Linker'
	g++ $(LDFLAGS) -o $(PROGRAM) $(OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

src:
		$(MAKE) -C src $@

clean:
		$(MAKE) -C src $@
		rm -f $(PROGRAM)
		

.PHONY: src clean tshow

tshow:
	@echo "######################################################################################################"
	@echo "######## optimize settings: $(InfoTextLib), $(InfoTextSrc)"
	@echo "######################################################################################################"

