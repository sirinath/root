# Module.mk for gl module
# Copyright (c) 2000 Rene Brun and Fons Rademakers
#
# Author: Fons Rademakers, 29/2/2000

MODNAME      := gl
MODDIR       := $(ROOT_SRCDIR)/graf3d/$(MODNAME)
MODDIRS      := $(MODDIR)/src
MODDIRI      := $(MODDIR)/inc

GLDIR        := $(MODDIR)
GLDIRS       := $(GLDIR)/src
GLDIRI       := $(GLDIR)/inc

##### libRGL #####
GLL          := $(MODDIRI)/LinkDef.h
GLDS         := $(call stripsrc,$(MODDIRS)/G__RGL.cxx)
GLDO         := $(GLDS:.cxx=.o)
GLDH         := $(GLDS:.cxx=.h)

GLH          := $(filter-out $(MODDIRI)/LinkDef%,$(wildcard $(MODDIRI)/*.h))
GLS          := $(filter-out $(MODDIRS)/G__%,$(wildcard $(MODDIRS)/*.cxx))

# Excluded from win32 builds
ifeq ($(ARCH),win32)
GLS          := $(filter-out $(MODDIRS)/TX11GL.cxx, $(GLS))
GLH          := $(filter-out $(MODDIRI)/TX11GL.h, $(GLH))
endif
# Excluded from OSX Cocoa builds
ifeq ($(BUILDCOCOA),yes)
GLS          := $(filter-out $(MODDIRS)/TX11GL.cxx, $(GLS))
GLH          := $(filter-out $(MODDIRI)/TX11GL.h, $(GLH))
endif

# Excluded from rootcint
GLH1         := $(MODDIRI)/CsgOps.h \
                $(MODDIRI)/TGLIncludes.h $(MODDIRI)/TGLWSIncludes.h \
                $(MODDIRI)/TGLMarchingCubes.h \
		$(MODDIRI)/TKDEAdapter.h $(MODDIRI)/TGL5DPainter.h \
		$(MODDIRI)/TKDEFGT.h $(MODDIRI)/TGLIsoMesh.h

# Used by rootcint
GLH2         := $(filter-out $(GLH1), $(GLH))

ifneq ($(OPENGLLIB),)
GLLIBS       := $(OPENGLLIBDIR) $(OPENGLULIB) $(OPENGLLIB) \
                $(XLIBS) -lm
endif
ifeq ($(ARCH),win32)
GLLIBS       := opengl32.lib glu32.lib
endif
ifeq ($(BUILDCOCOA),yes)
GLLIBS       := -framework OpenGL
endif

GLO          := $(call stripsrc,$(GLS:.cxx=.o))
GLDEP        := $(GLO:.o=.d) $(GLDO:.o=.d) $(GLO1:.o=.d)

GLLIB        := $(LPATH)/libRGL.$(SOEXT)
GLMAP        := $(GLLIB:.$(SOEXT)=.rootmap)

# used in the main Makefile
ALLHDRS      += $(patsubst $(MODDIRI)/%.h,include/%.h,$(GLH))
ALLLIBS      += $(GLLIB)
ALLMAPS      += $(GLMAP)

# include all dependency files
INCLUDEFILES += $(GLDEP)

##### local rules #####
.PHONY:         all-$(MODNAME) clean-$(MODNAME) distclean-$(MODNAME)

include/%.h:    $(GLDIRI)/%.h
		cp $< $@

$(GLLIB):       GLLIBCXXMODULE $(GLO) $(GLDO) $(ORDER_) $(MAINLIBS) $(GLLIBDEP) $(FTGLLIB) \
                $(GLEWLIB)
		@$(MAKELIB) $(PLATFORM) $(LD) "$(LDFLAGS)" \
		   "$(SOFLAGS)" libRGL.$(SOEXT) $@ "$(GLO) $(GLO1) $(GLDO)" \
		   "$(GLLIBEXTRA) $(FTGLLIBDIR) $(FTGLLIBS) \
		    $(GLEWLIBDIR) $(GLEWLIBS) $(GLLIBS)"

$(call pcmrule,GL)
	$(noop)

$(GLDS):	$(GLH2) $(GLL) $(ROOTCLINGEXE) $(call pcmdep,GL)
		$(MAKEDIR)
		@echo "Generating dictionary $@..."
		$(ROOTCLINGSTAGE2) -f $@ $(call dictModule,GL) -c -writeEmptyRootPCM $(CINTFLAGS) $(GLH2) $(GLL)

$(GLMAP):	$(GLH2) $(GLL) $(ROOTCLINGEXE) $(call pcmdep,GL)
		$(MAKEDIR)
		@echo "Generating rootmap $@..."
		$(ROOTCLINGSTAGE2) -r $(GLDS) $(call dictModule,GL) -c $(CINTFLAGS) $(GLH2) $(GLL)

all-$(MODNAME): $(GLLIB)
clean-$(MODNAME):
		@rm -f $(GLO) $(GLDO)

clean::         clean-$(MODNAME)

distclean-$(MODNAME): clean-$(MODNAME)
		@rm -f $(GLDEP) $(GLLIB) $(GLDS) $(GLDH) $(GLMAP)

distclean::     distclean-$(MODNAME)

##### extra rules ######
ifeq ($(ARCH),win32)
GLLIBCXXMODULE $(GLO) $(GLDO): CXXFLAGS += $(OPENGLINCDIR:%=-I%) -I$(WIN32GDKDIR)/gdk/src \
                            $(GDKDIRI:%=-I%) $(GLIBDIRI:%=-I%)
$(GLDS):        CINTFLAGS += $(OPENGLINCDIR:%=-I%) -I$(WIN32GDKDIR)/gdk/src \
                             $(GDKDIRI:%=-I%) $(GLIBDIRI:%=-I%)
else
GLLIBCXXMODULE $(GLO) $(GLDO): CXXFLAGS += $(OPENGLINCDIR:%=-I%)
$(GLDS):        CINTFLAGS += $(OPENGLINCDIR:%=-I%)
endif

$(call stripsrc,$(GLDIRS)/TGLText.o): $(FREETYPEDEP)
$(call stripsrc,$(GLDIRS)/TGLText.o): CXXFLAGS += $(FREETYPEINC) $(FTGLINC) $(FTGLINCDIR:%=-I%) $(FTGLCPPFLAGS)

$(call stripsrc,$(GLDIRS)/TGLFontManager.o): $(FREETYPEDEP)
$(call stripsrc,$(GLDIRS)/TGLFontManager.o): CXXFLAGS += $(FREETYPEINC) $(FTGLINC) $(FTGLINCDIR:%=-I%) $(FTGLCPPFLAGS)
GLLIBCXXMODULE $(GLO): CXXFLAGS += $(GLEWINCDIR:%=-I%) $(GLEWCPPFLAGS)

# Optimize dictionary with stl containers.
GLLIBCXXMODULE $(GLDO): NOOPT = $(OPT)

ifeq ($(MACOSX_GLU_DEPRECATED),yes)
GLLIBCXXMODULE $(GLO) $(GLDO): CXXFLAGS += -Wno-deprecated-declarations
endif

# glew.h is special (see $ROOTSYS/build/unix/module.modulemap for details.
# We need to prebuild a pcm disabling the system module maps.
# A pcm per set of options is necessary, this is why this is duplicated in eve and glew, too.
GLLIBCXXMODULE:
ifneq ($(CXXMODULES),)
	@echo "Prebuilding pcm for glew.h"
	@echo '#include "TGLIncludes.h"' | \
	$(CXX) $(CXXFLAGS) -fno-implicit-module-maps -xc++ -c -
endif
