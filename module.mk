
.PHONY : buld
build:

THISDIR     := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
MODNAME     := $(notdir $(realpath .))

ALLFILESWITHEXTENSION = $(sort $(patsubst ./%,%,$(shell find . -name '*.$(1)')))
ALLCFILES   := $(call ALLFILESWITHEXTENSION,c)
ALLCPPFILES := $(call ALLFILESWITHEXTENSION,cpp)

TESTCFILES   := $(filter test/%,$(ALLCFILES))
TESTCPPFILES := $(filter test/%,$(ALLCPPFILES))

SRCCFILES    := $(filter-out $(TESTCFILES),$(ALLCFILES))
SRCCPPFILES  := $(filter-out $(TESTCPPFILES),$(ALLCPPFILES))

TESTBINDIR   := ../bin
TESTEXE      := $(TESTBINDIR)/$(MODNAME)

TESTTSDIR    := ../ts
TESTTS       := $(TESTTSDIR)/$(MODNAME).ts

OBJDIR       := ../obj/$(MODNAME)

SRCCOBJS     := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCCFILES))
SRCCPPOBJS   := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SRCCPPFILES))
TESTCOBJS    := $(patsubst %.c,$(OBJDIR)/%.o,$(TESTCFILES))
TESTCPPOBJS  := $(patsubst %.cpp,$(OBJDIR)/%.o,$(TESTCPPFILES))

GENHDRFROM   := $(shell grep -l HEADER_START $(SRCCFILES))
GENHDRDIR    := $(THISDIR)/derived/$(MODNAME)
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
$(SRCCPPOBJS) $(TESTCPPOBJS) : CFLAGS+=-std=c++17

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




