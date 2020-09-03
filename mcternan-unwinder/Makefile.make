
# Select the compiler to use
CC=ARMCC
#CC=TCC

# Optimisation options
OPT?=-O1

# General flags
CFLAGS=-I. -DON_PC $(OPT) -DUNW_DEBUG -DUPGRADE_ARM_STACK_UNWIND

MAINFILES=unwarminder.o unwarm.o unwarm_thumb.o unwarm_arm.o unwarmmem.o 

all: simplefunc.s unwind.axf unwind.sym 

unwind.axf: CC+=--apcs=/interwork
unwind.axf: $(MAINFILES) simplefunc.o client.o
	$(CC) -o $@ $^

unwind.sym: unwind.axf
	fromElf -o $@ --text -s $^ 

unwind: CC=gcc
unwind: CFLAGS+=-DSIM_CLIENT
unwind: CFLAGS+=-Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align \
				-Wunreachable-code -Wpointer-arith -Waggregate-return \
				-Wpadded -ansi -pedantic
unwind: $(MAINFILES) simclient.o
	$(CC) -o $@ $^
	
%.s: %.c
	$(CC) -S $(CFLAGS) $^

run: all
	armsd unwind.axf
	
lint: OPT=
lint: $(MAINFILES:.o=.c) client.c
	splint -booltype Boolean -boolfalse FALSE -booltrue TRUE -retvalint +charindex -type -predboolint $(CFLAGS) $^	
	
clean: 
	rm -f *.s *.o unwind.axf unwind unwind.exe

copy:
	cp unw*.c /cygdrive/C/tplgsm/kicode
	cp unw*.h /cygdrive/C/tplgsm/kiinc

.PHONY: clean copy

# DO NOT DELETE
