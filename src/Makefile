include ../Makefile.common

OBJS+=AsyncSerial.o
OBJS+=crc.o
OBJS+=ssnproxy.o

all: src

src: src.a

src.a: $(OBJS)
#	$(AR) cr src.a $(OBJS)

.PHONY: src clean tshow

clean:
	rm -f src.a $(OBJS)
	
%.o: %.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CFLAGS) -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

%.o: %.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	g++ $(CFLAGS) -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '



tshow:
	@echo "######################################################################################################"
	@echo "######## optimize settings: $(InfoTextLib), $(InfoTextSrc)"
	@echo "######################################################################################################"
	
