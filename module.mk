
.PHONY : buld
build:

MACHINE     := $(shell uname -m)
NATIVE_OS   := $(shell uname -o)
ifeq ($(NATIVE_OS),GNU/Linux)
  OS := linux
else ifeq ($(NATIVE_OS),Darwin)
  OS := macos
else
  $(error Unknown OS $(NATIVE_OS))
endif

ARCHDIR     := $(OS)/$(MACHINE)

THISDIR     := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
MODNAME     := $(notdir $(realpath .))

DERIVEDDIR  := $(THISDIR)/derived/$(ARCHDIR)/$(MODNAME)
TESTBINDIR  := $(DERIVEDDIR)
TESTTSDIR   := $(DERIVEDDIR)/ts
OBJDIR      := $(DERIVEDDIR)/obj
GENHDRDIR   := $(DERIVEDDIR)/include
BINDIR      := $(THISDIR)/bin/$(ARCHDIR)/$(MODNAME)

ALLFILESWITHEXTENSION = $(sort $(patsubst ./%,%,$(shell find . -name '*.$(1)')))
ALLCFILES   := $(call ALLFILESWITHEXTENSION,c)
ALLCPPFILES := $(call ALLFILESWITHEXTENSION,cpp)

TESTCFILES   := $(filter test/%,$(ALLCFILES))
TESTCPPFILES := $(filter test/%,$(ALLCPPFILES))

SRCCFILES    := $(filter-out $(TESTCFILES),$(ALLCFILES))
SRCCPPFILES  := $(filter-out $(TESTCPPFILES),$(ALLCPPFILES))

TESTEXE      := $(TESTBINDIR)/test

TESTTS       := $(TESTTSDIR)/$(MODNAME).unit.ts
SYSTEMTESTTS := $(TESTTSDIR)/$(MODNAME).system.ts

EMBEDDEDOBJS := $(call ALLFILESWITHEXTENSION,o)

SRCCOBJS     := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCCFILES))
SRCCPPOBJS   := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SRCCPPFILES))
TESTCOBJS    := $(patsubst %.c,$(OBJDIR)/%.o,$(TESTCFILES))
TESTCPPOBJS  := $(patsubst %.cpp,$(OBJDIR)/%.o,$(TESTCPPFILES))

MAINC        := $(if $(SRCCFILES),$(shell grep -l TEST_MODE $(SRCCFILES)))
MAINCPP      := $(if $(SRCCPPFILES),$(shell grep -l TEST_MODE $(SRCCPPFILES)))

MAINCOBJS    := $(patsubst %.c,$(OBJDIR)/%_main.o,$(MAINC))
MAINCPPOBJS  := $(patsubst %.cpp,$(OBJDIR)/%_main.o,$(MAINCPP))

MAINOBJS     := $(sort $(MAINCOBJS) $(MAINCPPOBJS))
EXE          := $(patsubst $(OBJDIR)/%_main.o,$(BINDIR)/%,$(MAINOBJS))
$(info $(EXE))

GENHDRFROM   := $(if $(SRCCFILES),$(shell grep -l HEADER_START $(SRCCFILES)))
GENHDR       := $(patsubst %.c,$(GENHDRDIR)/%.h,$(GENHDRFROM))

$(GENHDR) : $(GENHDRDIR)/%.h : %.c $(MAKEFILE_LIST) | $(GENHDRDIR)
	@echo "#ifndef $*_h" > $@
	@echo "#define $*_h" >> $@
	@echo "#ifdef __cplusplus" >> $@
	@echo 'extern "C" {' >> $@
	@echo '#endif' >> $@
	awk '/HEADER_START/{flag=1; next} /HEADER_END/{print buffer; buffer=""; flag=0} flag {buffer=buffer $$0 "\n"}' $< >> $@
	@echo "#ifdef __cplusplus" >> $@
	@echo '}' >> $@
	@echo '#endif' >> $@
	@echo '#endif' >> $@


3PROOT           := $(THISDIR)/../../3p
GOOGLETEST_ROOT  ?= $(3PROOT)/googletest
BOOST_ROOT       ?= $(3PROOT)/boost_1_89_0

OBJS         := $(sort $(SRCCOBJS) $(SRCCPPOBJS)) $(sort $(TESTCOBJS) $(TESTCPPOBJS))

DEPFILE       = $(patsubst %.o,%.d,$(1))

CFLAGS = -c $< -o $@ $(if $(DEBUG),-O0,-O2) -g -Wpedantic -Wall -Werror -MMD -MF $(call DEPFILE,$@) -MP

$(SRCCOBJS) $(TESTCOBJS) $(MAINCOBJS) : CFLAGS+=-std=c99 
$(SRCCPPOBJS) $(TESTCPPOBJS) $(MAINCPPOBJS) : CFLAGS+=-std=c++20 

$(SRCCPPOBJS) $(MAINCPPOBJS) : CFLAGS+=$(if $(BOOST_IN_SOURCE),-I$(BOOST_ROOT))

$(SRCCOBJS) $(SRCCPPOBJS) : CFLAGS+=-DTEST_MODE=1

$(TESTCOBJS) $(TESTCPPOBJS) : CFLAGS+=-I$(GOOGLETEST_ROOT)/googletest/include -I$(GOOGLETEST_ROOT)/googlemock/include -I$(BOOST_ROOT) -I$(GENHDRDIR)
$(TESTCOBJS) $(TESTCPPOBJS) : | $(GENHDR)

$(SRCCOBJS) $(TESTCOBJS) : $(OBJDIR)/%.o : %.c 
	gcc $(CFLAGS)

$(SRCCPPOBJS) $(TESTCPPOBJS) : $(OBJDIR)/%.o : %.cpp
	g++ $(CFLAGS)

$(MAINCOBJS) : $(OBJDIR)/%_main.o : %.c 
	gcc $(CFLAGS)

$(MAINCPPOBJS) : $(OBJDIR)/%_main.o : %.cpp
	g++ $(CFLAGS)

$(TESTEXE) : $(OBJS) $(MAKEFILE_LIST) | $(TESTBINDIR)
	g++ -o $@ $(OBJS) $(GOOGLETEST_ROOT)/build/lib/libgmock.a $(GOOGLETEST_ROOT)/build/lib/libgtest.a

# if an embedded obj file has a corresponding _noasan.o file, then 
# just link in the _nosasan.o file.
NOASANEMBEDDEDOBJS := $(filter %_noasan.o,$(EMBEDDEDOBJS))
EMBEDEDDOBJS       := $(filter-out $(patsubst %_noasan.o,%.o,$(NOASANEMBEDDEDOBJS)),$(EMBEDDEDOBJS))

EXESRCOBJS := $(sort \
  $(filter-out $(patsubst %.c,$(OBJDIR)/%.o,$(MAINC)),$(SRCCOBJS)) \
  $(filter-out $(patsubst %.cpp,$(OBJDIR)/%.o,$(MAINCPP)),$(SRCCPPOBJS)))

$(EXE) : $(BINDIR)/% : $(OBJDIR)/%_main.o $(EXESRCOBJS) $(EMBEDEDDOBJS) | $(BINDIR)
	g++ -o $@ $< $(EXESRCOBJS) $(EMBEDEDDOBJS)

$(TESTTS) : $(TESTEXE) | $(TESTTSDIR)
	$(RM) $@
	$(TESTEXE)
	touch $@

SYSTEMTEST   := $(wildcard test/*_system.pl)
SYSTEMTESTTS := $(patsubst test/%_system.pl,$(TESTTSDIR)/%_system.ts,$(SYSTEMTEST))
$(SYSTEMTESTTS) : $(TESTTSDIR)/%_system.ts : test/%_system.pl $(TESTTS) $(EXE) | $(TESTTSDIR)
	$(RM) $@
	perl $< $(BINDIR)
	touch $@


build : $(OBJS) $(TESTEXE) $(EXE) 
ifndef NORUNTESTS
  build : $(TESTTS) $(SYSTEMTESTTS)
endif

.PHONY : clean
clean : 
	$(RM) -rf $(OBJDIR) $(TESTEXE) $(TESTTS)

ALLOBJDIRS := $(sort $(patsubst %/,%,$(dir $(OBJS))))
$(OBJS) : $(MAKEFILE_LIST) | $(ALLOBJDIRS)

$(ALLOBJDIRS) $(TESTBINDIR) $(TESTTSDIR) $(GENHDRDIR) $(BINDIR) :
	mkdir -p $@

-include $(call DEPFILE,$(OBJS))




