PROG = fault-test

PROG_LIST = $(PROG) \
	$(PROG)-omit-frame \
	$(PROG)-exceptions \
	$(PROG)-unwind \
	$(PROG)-stripped \
	$(PROG)-omit-frame-stripped \
	$(PROG)-exceptions-stripped \
	$(PROG)-unwind-stripped \

PROG_DISASSEMBLY = $(PROG).S \
	$(PROG)-omit-frame.S \
	$(PROG)-exceptions.S

all: $(PROG_LIST) $(PROG_DISASSEMBLY)

$(PROG): $(PROG).c
	$(CROSS_COMPILE)gcc -g $^ -o $@

$(PROG)-omit-frame: $(PROG).c
	$(CROSS_COMPILE)gcc -g -fomit-frame-pointer $^ -o $@

$(PROG)-exceptions: $(PROG).c
	$(CROSS_COMPILE)gcc -g -fexceptions $^ -o $@

$(PROG)-unwind: $(PROG).c
	$(CROSS_COMPILE)gcc -g -funwind-tables $^ -o $@

$(PROG)-stripped: $(PROG)
	$(CROSS_COMPILE)strip --strip-unneeded $^ -o $@

$(PROG)-omit-frame-stripped: $(PROG)-omit-frame
	$(CROSS_COMPILE)strip --strip-unneeded $^ -o $@

$(PROG)-exceptions-stripped: $(PROG)-exceptions
	$(CROSS_COMPILE)strip --strip-unneeded $^ -o $@

$(PROG)-unwind-stripped: $(PROG)-unwind
	$(CROSS_COMPILE)strip --strip-unneeded $^ -o $@

$(PROG).S: $(PROG)
	$(CROSS_COMPILE)objdump -d -S $^ >$@

$(PROG)-omit-frame.S: $(PROG)-omit-frame
	$(CROSS_COMPILE)objdump -d -S $^ >$@

$(PROG)-exceptions.S: $(PROG)-exceptions
	$(CROSS_COMPILE)objdump -d -S $^ >$@

$(PROG)-unwind.S: $(PROG)-unwind
	$(CROSS_COMPILE)objdump -d -S $^ >$@

clean:
	-rm $(PROG_LIST) $(PROG_DISASSEMBLY)

install:
	ttc cp $(PROG) target:/tmp
	ttc cp $(PROG)-omit-frame target:/tmp
	ttc cp $(PROG)-exceptions target:/tmp
	ttc cp $(PROG)-unwind target:/tmp
	ttc cp $(PROG)-stripped target:/tmp
	ttc cp $(PROG)-omit-frame-stripped target:/tmp
	ttc cp $(PROG)-exceptions-stripped target:/tmp
	ttc cp $(PROG)-unwind-stripped target:/tmp

