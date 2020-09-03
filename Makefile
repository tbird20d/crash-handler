# keep these in-sync with the #defines in crash_handler.c
VERSION=0
REVISION=6

PROG = crash_handler

OBJECTS = crash_handler.o \
	utility.o \
	journal.o \
	table-pr-support.o \
	table-unwind-arm.o \
	guess-unwinder.o

$(PROG): $(OBJECTS)
	$(CROSS_COMPILE)gcc $^ -o $@

%.o: %.c
	$(CROSS_COMPILE)gcc -c $< -o $@

clean:
	rm $(PROG) $(OBJECTS)

distclean:
	-make clean
	-make -C test clean

default_install:
	# this won't work unless you are self-hosted on ARM
	cp $(PROG) /tmp
	/tmp/$(PROG) --install
	 
sony_install:
	ttc cp $(PROG) target:/tmp
	ttc run "/tmp/$(PROG) --install"

# select the installation method by uncommenting one of these:
#install: default_install
install: sony_install

distribution:
	-make distclean
	cd .. ; ln -s crash_handler crash_handler-${VERSION}.${REVISION} ; \
	tar -czvf crash_handler-${VERSION}.${REVISION}.tgz crash_handler-${VERSION}.${REVISION}/* ; \
	unlink crash_handler-${VERSION}.${REVISION}
	
	 
help:
	@echo "Here are some supported targets for this Makefile:"
	@echo 
	@echo "  install:      install the crash_handler program"
	@echo "  clean:        remove generated files"
	@echo "  distclean:    remove generated files, including in subdirs"
	@echo "  distribution: create a distribution tarball"
