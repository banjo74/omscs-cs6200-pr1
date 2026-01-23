
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

ALLFILESWITHEXTENSION = $(sort $(patsubst ./%,%,$(shell find . -name '*.$(1)')))
ALLCFILES   := $(call ALLFILESWITHEXTENSION,c)
ALLCPPFILES := $(call ALLFILESWITHEXTENSION,cpp)

TESTCFILES   := $(filter test/%,$(ALLCFILES))
TESTCPPFILES := $(filter test/%,$(ALLCPPFILES))

SRCCFILES    := $(filter-out $(TESTCFILES),$(ALLCFILES))
SRCCPPFILES  := $(filter-out $(TESTCPPFILES),$(ALLCPPFILES))

TESTEXE      := $(TESTBINDIR)/test

TESTTS       := $(TESTTSDIR)/$(MODNAME).ts

SRCCOBJS     := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCCFILES))
SRCCPPOBJS   := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SRCCPPFILES))
TESTCOBJS    := $(patsubst %.c,$(OBJDIR)/%.o,$(TESTCFILES))
TESTCPPOBJS  := $(patsubst %.cpp,$(OBJDIR)/%.o,$(TESTCPPFILES))

GENHDRFROM   := $(shell grep -l HEADER_START $(SRCCFILES))
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

CFLAGS = -c $< -o $@ -O2 -g -Wpedantic -Wall -Werror -DTEST_MODE=1 -MMD -MF $(call DEPFILE,$@) -MP

$(SRCCOBJS) $(TESTCOBJS)     : CFLAGS+=-std=c99
$(SRCCPPOBJS) $(TESTCPPOBJS) : CFLAGS+=-std=c++20

$(TESTCOBJS) $(TESTCPPOBJS) : CFLAGS+=-I$(GOOGLETEST_ROOT)/googletest/include -I$(GOOGLETEST_ROOT)/googlemock/include -I$(BOOST_ROOT) -I$(GENHDRDIR)
$(TESTCOBJS) $(TESTCPPOBJS) : | $(GENHDR)

$(SRCCOBJS) $(TESTCOBJS) : $(OBJDIR)/%.o : %.c 
	gcc $(CFLAGS)

$(SRCCPPOBJS) $(TESTCPPOBJS) : $(OBJDIR)/%.o : %.cpp
	g++ $(CFLAGS)

$(TESTEXE) : $(OBJS) $(MAKEFILE_LIST) | $(TESTBINDIR)
	g++ -o $@ $(OBJS) $(GOOGLETEST_ROOT)/build/lib/libgmock.a $(GOOGLETEST_ROOT)/build/lib/libgtest.a

$(TESTTS) : $(TESTEXE) | $(TESTTSDIR)
	$(RM) $@
	$(TESTEXE)
	touch $@

build : $(OBJS) $(TESTEXE) $(TESTTS)

.PHONY : clean
clean : 
	$(RM) -rf $(OBJDIR) $(TESTEXE) $(TESTTS)

ALLOBJDIRS := $(sort $(patsubst %/,%,$(dir $(OBJS))))
$(OBJS) : $(MAKEFILE_LIST) | $(ALLOBJDIRS)

$(ALLOBJDIRS) $(TESTBINDIR) $(TESTTSDIR) $(GENHDRDIR) :
	mkdir -p $@

-include $(call DEPFILE,$(OBJS))




