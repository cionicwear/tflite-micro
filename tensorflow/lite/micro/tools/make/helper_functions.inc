DOWNLOAD_SCRIPT := $(MAKEFILE_DIR)/download_and_extract.sh

# Reverses a space-separated list of words.
reverse = $(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1)))) $(firstword $(1))

# Get macros only (i.e. the ones starting with -D) from two lists and remove duplicates
getmacros = $(patsubst -D%,%,$(filter -D%,$(sort $(filter -D%, $(1)) $(filter -D%, $(2)))))

# Look for platform or target-specific implementation files to replace reference
# implementations with, given a tag. These are expected to occur in subfolders
# of a directory where a reference implementation exists, and have the same
# interface and header file. For example,
# tensorflow/lite/micro/examples/micro_speech/audio_provider.cc
# defines a module for supplying audio data, but since no platform or OS can be
# presumed, it just always returns zeroes for its samples. The MacOS-specific
# tensorflow/lite/micro/examples/micro_speech/osx/audio_provider.cc
# has an implementation that relies on CoreAudio, and there are equivalent
# versions for other operating systems.
# The specific implementation yielded by the first tag in the list that produces
# a match is returned, else the reference version if none of the tags produce a
# match.
# All lists of source files are put through this substitution process with the
# tags of their target OS and architecture, so that implementations can be added
# by simply placing them in the file tree, with no changes to the build files
# needed.
# One confusing thing about this implementation is that we're using wildcard to
# act as a 'does file exist?' function, rather than expanding an expression.
# Wildcard will return an empty string if given a plain file path with no actual
# wildcards, if the file doesn't exist, so taking the first word of the list
# between that and the reference path will pick the specialized one if it's
# available.
# Another fix is that originally if neither file existed(either the original or
# a specialized version) this would return an empty string.Because this is
# sometimes called on third party library files before they've been downloaded,
# this caused mysterious errors, so an initial if conditional was added so that
# specializations are only looked for if the original file exists.
substitute_specialized_implementation = \
  $(if $(wildcard $(1)),$(firstword $(wildcard $(dir $(1))$(2)/$(notdir $(1))) $(wildcard $(1))),$(1))
substitute_specialized_implementations = \
  $(foreach source,$(1),$(call substitute_specialized_implementation,$(source),$(2)))

# Tests and project generation targets use this entrypoint for to get the
# specialized sources. It should be avoided for any new functionality.
# The only argument is a list of file paths.
specialize = $(call substitute_specialized_implementations,$(1),$(TARGET))

# TODO(b/143904317): It would be better to have the dependency be
# THIRD_PARTY_TARGETS instead of third_party_downloads. However, that does not
# quite work for the generate_project functions.
#
# Creates a set of rules to build a standalone makefile project for an
# executable, including all of the source and header files required in a
# separate folder and a simple makefile.
# Arguments are:
# 1 - Project type (make, etc).
# 2 - Project file template name.
# 3 - Name of executable.
# 4 - List of C/C++ source files needed to build the target.
# 5 - List of C/C++ header files needed to build the target.
# 6 - Linker flags required.
# 7 - C++ compilation flags needed.
# 8 - C compilation flags needed.
# 9 - Target Toolchian root directory
# 10 - Target Toolchain prefix
# Calling eval on the output will create a <Name>_makefile target that you
# can invoke to create the standalone project.
define generate_project
$(PRJDIR)$(3)/$(1)/%: % third_party_downloads
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(3)/cmake/boards/%: tensorflow/lite/micro/examples/$(3)/zephyr_riscv/boards/%
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(3)/cmake/%: tensorflow/lite/micro/examples/$(3)/zephyr_riscv/%
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(3)/$(1)/third_party/%: tensorflow/lite/micro/tools/make/downloads/% third_party_downloads
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(3)/$(1)/%: tensorflow/lite/micro/tools/make/templates/%.tpl
	@mkdir -p $$(dir $$@)
	@sed -E 's#\%\{SRCS\}\%#$(4)#g' $$< | \
	sed -E 's#\%\{EXECUTABLE\}\%#$(3)#g' | \
	sed -E 's#\%\{LINKER_FLAGS\}\%#$(6)#g' | \
	sed -E 's#\%\{CXX_FLAGS\}\%#$(7)#g' | \
	sed -E 's#\%\{CC_FLAGS\}\%#$(8)#g' | \
	sed -E 's#\%\{TARGET_TOOLCHAIN_ROOT\}\%#$(9)#g' | \
	sed -E 's#\%\{TARGET_TOOLCHAIN_PREFIX\}\%#$(10)#g' > $$@

$(PRJDIR)$(3)/$(1)/keil_project.uvprojx: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/keil_project.uvprojx.tpl
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/generate_keil_project.py \
        --input_template=$$< --output_file=$$@ --executable=$(3) \
        --srcs="$(4)" --hdrs="$(5)" --include_paths="$$(PROJECT_INCLUDES)"

$(PRJDIR)$(3)/$(1)/.vscode/tasks.json : $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/tasks.json.$(1).tpl
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

generate_$(3)_$(1)_project: $(addprefix $(PRJDIR)$(3)/$(1)/, $(4) $(5) $(2))

list_$(3)_$(1)_files:
	@echo $(4) $(5)

ALL_PROJECT_TARGETS += generate_$(3)_$(1)_project
endef

# Creates a set of rules to build a standalone makefile project for the ARC platform
# including all of the source and header files required in a
# separate folder and a simple makefile.
# Arguments are:
# 1 - Project type (make, etc).
# 2 - Project file template name.
# 3 - Name of executable.
# 4 - List of C/C++ source files needed to build the target.
# 5 - List of C/C++ header files needed to build the target.
# 6 - Linker flags required.
# 7 - C++ compilation flags needed.
# 8 - C compilation flags needed.

# Calling eval on the output will create a <Name>_makefile target that you
# can invoke to create the standalone project.
define generate_arc_project

ifeq ($(TARGET_ARCH), arc)

$(PRJDIR)$(3)/$(1)/Makefile: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/arc/arc_app_makefile.tpl
	@mkdir -p $$(dir $$@)
	@sed -E 's#\%\{SRCS\}\%#$(4)#g' $$< | \
	sed -E 's#\%\{CC\}\%#$(CC_TOOL)#g' | \
	sed -E 's#\%\{CXX\}\%#$(CXX_TOOL)#g' | \
	sed -E 's#\%\{LD\}\%#$(LD_TOOL)#g' | \
	sed -E 's#\%\{EXECUTABLE\}\%#$(3).elf#g' | \
	sed -E 's#\%\{LINKER_FLAGS\}\%#$(6)#g' | \
	sed -E 's#\%\{CXX_FLAGS\}\%#$(7)#g' | \
	sed -E 's#\%\{CC_FLAGS\}\%#$(8)#g' | \
	sed -E 's#\%\{EXTRA_APP_SETTINGS\}\%#$(ARC_EXTRA_APP_SETTINGS)#g' | \
	sed -E 's#\%\{EXTRA_APP_RULES\}\%#$(ARC_EXTRA_APP_RULES)#g' | \
	sed -E 's#\%\{BIN_DEPEND\}\%#$(ARC_BIN_DEPEND)#g' | \
	sed -E 's#\%\{BIN_RULE\}\%#$(ARC_BIN_RULE)#g' | \
	sed -E 's#\%\{EXTRA_RM_TARGETS\}\%#$(ARC_EXTRA_RM_TARGETS)#g' | \
	sed -E 's#\%\{APP_RUN_CMD\}\%#$(ARC_APP_RUN_CMD)#g' | \
	sed -E 's#\%\{APP_DEBUG_CMD\}\%#$(ARC_APP_DEBUG_CMD)#g' | \
	sed -E 's#\%\{EXTRA_EXECUTE_RULES\}\%#$(ARC_EXTRA_EXECUTE_RULES)#g' > $$@

$(PRJDIR)$(3)/$(1)/%: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/arc/%.tpl
	@cp $$< $$@

$(foreach var,$(ARC_TARGET_COPY_FILES), $(eval $(call path_changing_copy_file,\
    $(PRJDIR)$(3)/$(1)/$(word 1, $(subst !, ,$(var))),\
    $(word 2, $(subst !, ,$(var))))))

endif
endef


define generate_ceva_bx1_project
ifeq ($(TARGET), ceva)
ifeq ($(TARGET_ARCH), CEVA_BX1)

$(PRJDIR)$(3)/$(1)/Makefile: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/ceva/ceva_app_makefile_v18.0.5.tpl
	@mkdir -p $$(dir $$@)
	@sed -E 's#\%\{SRCS\}\%#$(4)#g' $$< | \
	sed -E 's#\%\{CC\}\%#$(CC_TOOL)#g' | \
	sed -E 's#\%\{CXX\}\%#$(CXX_TOOL)#g' | \
	sed -E 's#\%\{LD\}\%#$(LD_TOOL)#g' | \
	sed -E 's#\%\{EXECUTABLE\}\%#$(3).elf#g' | \
	sed -E 's#\%\{LD_FLAGS\}\%#$(6)#g' | \
	sed -E 's#\%\{CXX_FLAGS\}\%#$(7)#g' | \
	sed -E 's#\%\{CC_FLAGS\}\%#$(8)#g' > $$@

$(PRJDIR)$(3)/$(1)/%: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/ceva/%.tpl
	@cp $$< $$@

$(foreach var,$(CEVA_TARGET_FILES_DIRS),$(eval $(call path_changing_copy_file,$(PRJDIR)$(3)/$(1),$(var))))

endif
endif
endef

define generate_ceva_sp500_project
ifeq ($(TARGET), ceva)
ifeq ($(TARGET_ARCH), CEVA_SP500)

$(PRJDIR)$(3)/$(1)/Makefile: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/ceva_SP500/ceva_app_makefile.tpl
	@mkdir -p $$(dir $$@)
	@sed -E 's#\%\{SRCS\}\%#$(4)#g' $$< | \
	sed -E 's#\%\{CC\}\%#$(CC_TOOL)#g' | \
	sed -E 's#\%\{CXX\}\%#$(CXX_TOOL)#g' | \
	sed -E 's#\%\{LD\}\%#$(LD_TOOL)#g' | \
	sed -E 's#\%\{EXECUTABLE\}\%#$(3).elf#g' | \
	sed -E 's#\%\{LD_FLAGS\}\%#$(6)#g' | \
	sed -E 's#\%\{CXX_FLAGS\}\%#$(7)#g' | \
	sed -E 's#\%\{CC_FLAGS\}\%#$(8)#g' | \
	sed -E 's#\%\{EXTRA_APP_SETTINGS\}\%#$(ARC_EXTRA_APP_SETTINGS)#g' | \
	sed -E 's#\%\{EXTRA_APP_RULES\}\%#$(ARC_EXTRA_APP_RULES)#g' | \
	sed -E 's#\%\{BIN_DEPEND\}\%#$(ARC_BIN_DEPEND)#g' | \
	sed -E 's#\%\{BIN_RULE\}\%#$(ARC_BIN_RULE)#g' | \
	sed -E 's#\%\{EXTRA_RM_TARGETS\}\%#$(ARC_EXTRA_RM_TARGETS)#g' | \
	sed -E 's#\%\{APP_RUN_CMD\}\%#$(ARC_APP_RUN_CMD)#g' | \
	sed -E 's#\%\{APP_DEBUG_CMD\}\%#$(ARC_APP_DEBUG_CMD)#g' | \
	sed -E 's#\%\{EXTRA_EXECUTE_RULES\}\%#$(ARC_EXTRA_EXECUTE_RULES)#g' > $$@

$(PRJDIR)$(3)/$(1)/%: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/ceva_SP500/%.tpl
	@cp $$< $$@

$(foreach var,$(CEVA_TARGET_FILES_DIRS),$(eval $(call path_changing_copy_file,$(PRJDIR)$(3)/$(1),$(var))))

endif
endif
endef





# Creates a set of rules to build a standalone Arduino project for an
# executable, including all of the source and header files required in a
# separate folder and a simple makefile.
# Arguments are:
# 1 - Project file template names.
# 2 - Name of executable.
# 3 - List of C/C++ source files needed to build the target.
# 4 - List of C/C++ header files needed to build the target.
# 5 - Linker flags required.
# 6 - C++ compilation flags needed.
# 7 - C compilation flags needed.
# Calling eval on the output will create a <Name>_makefile target that you
# can invoke to create the standalone project.
define generate_arduino_project

$(PRJDIR)$(2)/arduino/examples/%.c: tensorflow/lite/micro/examples/%.c
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=arduino \
        --is_example_source \
        --source_path="$$<" \
        --third_party_headers="$(4)" < $$< > $$@

$(PRJDIR)$(2)/arduino/examples/%.cpp: tensorflow/lite/micro/examples/%.cc
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=arduino \
        --is_example_source \
        --source_path="$$<" \
        --third_party_headers="$(4)" < $$< > $$@

$(PRJDIR)$(2)/arduino/examples/%.h: tensorflow/lite/micro/examples/%.h
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=arduino \
        --is_example_source \
        --source_path="$$<" \
        --third_party_headers="$(4)" < $$< > $$@

$(PRJDIR)$(2)/arduino/examples/%/main.ino: tensorflow/lite/micro/examples/%/main_functions.cc
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=arduino \
        --is_example_ino \
        --source_path="$$<" \
        --third_party_headers="$(4)" < $$< > $$@

$(PRJDIR)$(2)/arduino/src/%.cpp: %.cc
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=arduino \
        --third_party_headers="$(4)" < $$< > $$@

$(PRJDIR)$(2)/arduino/src/%.h: %.h third_party_downloads
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=arduino \
        --third_party_headers="$(4)" < $$< > $$@

$(PRJDIR)$(2)/arduino/LICENSE: LICENSE
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(2)/arduino/src/%: % third_party_downloads
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=arduino \
        --third_party_headers="$(4)" < $$< > $$@

$(PRJDIR)$(2)/arduino/src/third_party/%: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/downloads/% third_party_downloads
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=arduino \
        --third_party_headers="$(4)" < $$< > $$@

$(PRJDIR)$(2)/arduino/src/third_party/%.cpp: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/downloads/%.cc third_party_downloads
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=arduino \
        --third_party_headers="$(4)" < $$< > $$@

$(PRJDIR)$(2)/arduino/src/third_party/flatbuffers/include/flatbuffers/base.h: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/downloads/flatbuffers/include/flatbuffers/base.h third_party_downloads
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=arduino \
        --third_party_headers="$(4)" < $$< | \
        sed -E 's/utility\.h/utility/g' > $$@

$(PRJDIR)$(2)/arduino/src/third_party/kissfft/kiss_fft.h: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/downloads/kissfft/kiss_fft.h third_party_downloads
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=arduino \
        --third_party_headers="$(4)" < $$< | \
        sed -E 's@#include <string.h>@//#include <string.h> /* Patched by helper_functions.inc for Arduino compatibility */@g' > $$@

$(PRJDIR)$(2)/arduino/%: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/%
	@mkdir -p $$(dir $$@)
	@sed -E 's#\%\{SRCS\}\%#$(3)#g' $$< | \
	sed -E 's#\%\{EXECUTABLE\}\%#$(2)#g' | \
	sed -E 's#\%\{LINKER_FLAGS\}\%#$(5)#g' | \
	sed -E 's#\%\{CXX_FLAGS\}\%#$(6)#g' | \
	sed -E 's#\%\{CC_FLAGS\}\%#$(7)#g' > $$@

$(PRJDIR)$(2)/arduino/examples/$(2)/$(2).ino: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/arduino_example.ino
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(2)/arduino/src/TensorFlowLite.h: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/TensorFlowLite.h
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

# This would be cleaner if we broke up the list of dependencies into variables,
# but these get hard to define with the evaluation approach used to define make
# functions.
generate_$(2)_arduino_project: \
$(addprefix $(PRJDIR)$(2)/arduino/,         \
$(patsubst tensorflow/%,src/tensorflow/%,\
$(patsubst examples/%/main_functions.cpp,examples/%/main.ino,\
$(patsubst examples/%_test.cpp,examples/%_test.ino,\
$(patsubst tensorflow/lite/micro/examples/%,examples/%,\
$(patsubst third_party/%,src/third_party/%,\
$(patsubst %.cc,%.cpp,$(3))))))))                                     \
$(addprefix $(PRJDIR)$(2)/arduino/, \
$(patsubst tensorflow/%,src/tensorflow/%,\
$(patsubst tensorflow/lite/micro/examples/%,examples/%,\
$(patsubst third_party/%,src/third_party/%,$(4))))) \
$(addprefix $(PRJDIR)$(2)/arduino/,$(1)) \
$(PRJDIR)$(2)/arduino/src/TensorFlowLite.h

generate_$(2)_arduino_library_zip: generate_$(2)_arduino_project
	cp -r $(PRJDIR)$(2)/arduino $(PRJDIR)$(2)/tensorflow_lite
	python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/fix_arduino_subfolders.py $(PRJDIR)$(2)/tensorflow_lite
	@cd $(PRJDIR)$(2) && zip -q -r tensorflow_lite.zip tensorflow_lite

ALL_PROJECT_TARGETS += $(if $(findstring _test,$(2)),,generate_$(2)_arduino_library_zip)

ARDUINO_LIBRARY_ZIPS += $(if $(findstring _mock,$(2)),,$(if $(findstring _test,$(2)),,$(PRJDIR)$(2)/tensorflow_lite.zip))

endef

# Creates a set of rules to build a standalone ESP-IDF project for an
# executable, including all of the source and header files required in a
# separate folder.
# Arguments are:
# 1 - Project file template names.
# 2 - Name of executable.
# 3 - List of C/C++ source files needed to build the TF Micro component.
# 4 - List of C/C++ header files needed to build the TF Micro component.
# 5 - List of C/C++ source files needed to build this particular project.
# 6 - List of C/C++ header files needed to build this particular project.
# 7 - Linker flags required.
# 8 - C++ compilation flags needed.
# 9 - C compilation flags needed.
# 10 - List of includes.
define generate_esp_project
$(PRJDIR)$(2)/esp-idf/LICENSE: LICENSE
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(2)/esp-idf/%: tensorflow/lite/micro/examples/$(2)/esp/%
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(2)/esp-idf/main/%.cc: tensorflow/lite/micro/examples/$(2)/%.cc
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=esp \
        --is_example_source \
        --source_path="$$<" \
        < $$< > $$@

$(PRJDIR)$(2)/esp-idf/main/%.h: tensorflow/lite/micro/examples/$(2)/%.h
	@mkdir -p $$(dir $$@)
	@python $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/transform_source.py \
        --platform=esp \
        --is_example_source \
        --source_path="$$<" \
        < $$< > $$@

$(PRJDIR)$(2)/esp-idf/main/%: tensorflow/lite/micro/examples/$(2)/%
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(2)/esp-idf/components/tfmicro/%: % third_party_downloads
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(2)/esp-idf/components/tfmicro/third_party/%: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/downloads/% third_party_downloads
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(2)/esp-idf/sdkconfig.defaults: $(TENSORFLOW_ROOT)tensorflow/lite/micro/examples/$(2)/esp/sdkconfig.defaults
	@mkdir -p $$(dir $$@)
	@cp $$< $$@

$(PRJDIR)$(2)/esp-idf/%: $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/templates/esp/%.tpl
# Split the sources into 2 components:
# - Main component contains only the example's sources, relative from its dir.
# - TFL Micro component contains everything but the example sources.
  $(eval MAIN_SRCS := $(filter tensorflow/lite/micro/examples/%,$(5)))
  $(eval MAIN_SRCS_RELATIVE := $(patsubst tensorflow/lite/micro/examples/$(2)/%,%,$(MAIN_SRCS)))
  $(eval TFLM_SRCS := $(filter-out tensorflow/lite/micro/examples/%,$(5)) $(3))

	@mkdir -p $$(dir $$@)
	@sed -E 's#\%\{COMPONENT_SRCS\}\%#$(TFLM_SRCS)#g' $$< | \
	sed -E 's#\%\{MAIN_SRCS\}\%#$(MAIN_SRCS_RELATIVE)#g' | \
	sed -E 's#\%\{EXECUTABLE\}\%#$(2)#g' | \
	sed -E 's#\%\{COMPONENT_INCLUDES\}\%#$(10)#g' | \
	sed -E 's#\%\{LINKER_FLAGS\}\%#$(7)#g' | \
	sed -E 's#\%\{CXX_FLAGS\}\%#$(8)#g' | \
	sed -E 's#\%\{CC_FLAGS\}\%#$(9)#g' > $$@

generate_$(2)_esp_project: \
$(addprefix $(PRJDIR)$(2)/esp-idf/,\
$(patsubst tensorflow/%,components/tfmicro/tensorflow/%,\
$(patsubst third_party/%,components/tfmicro/third_party/%,\
$(patsubst tensorflow/lite/micro/examples/$(2)/%,main/%,$(3) $(4) $(5) $(6))))) \
$(addprefix $(PRJDIR)$(2)/esp-idf/,$(1))

ALL_PROJECT_TARGETS += generate_$(2)_esp_project
endef

# Specialized version of generate_project for TF Lite Micro test targets that
# automatically includes standard library files, so you just need to pass the
# test name and any extra source files required.
# Arguments are:
# 1 - Name of test.
# 2 - C/C++ source files implementing the test.
# 3 - C/C++ header files needed for the test.
# Calling eval on the output will create targets that you can invoke to
# generate the standalone project.
define generate_microlite_projects
$(call generate_project,make,$(MAKE_PROJECT_FILES) $($(1)_MAKE_PROJECT_FILES),$(1),$(MICROLITE_CC_SRCS) $(THIRD_PARTY_CC_SRCS) $(MICROLITE_CC_KERNEL_SRCS) $(2),$(MICROLITE_CC_HDRS) $(THIRD_PARTY_CC_HDRS) $(MICROLITE_TEST_HDRS) $(3),$(LDFLAGS) $(MICROLITE_LIBS),$(CXXFLAGS) $(GENERATED_PROJECT_INCLUDES), $(CCFLAGS) $(GENERATED_PROJECT_INCLUDES),$(TARGET_TOOLCHAIN_ROOT),$(TARGET_TOOLCHAIN_PREFIX))
$(call generate_arc_project,make,$(MAKE_PROJECT_FILES) $($(1)_MAKE_PROJECT_FILES),$(1),$(MICROLITE_CC_SRCS) $(THIRD_PARTY_CC_SRCS) $(MICROLITE_CC_KERNEL_SRCS) $(2),$(MICROLITE_CC_HDRS) $(THIRD_PARTY_CC_HDRS) $(MICROLITE_TEST_HDRS) $(3),$(LDFLAGS) $(GENERATED_PROJECT_LIBS),$(CXXFLAGS) $(GENERATED_PROJECT_INCLUDES), $(CCFLAGS) $(GENERATED_PROJECT_INCLUDES))
$(call generate_ceva_bx1_project,make,$(MAKE_PROJECT_FILES) $($(1)_MAKE_PROJECT_FILES),$(1),$(MICROLITE_CC_SRCS) $(THIRD_PARTY_CC_SRCS) $(MICROLITE_CC_KERNEL_SRCS) $(2),$(MICROLITE_CC_HDRS) $(THIRD_PARTY_CC_HDRS) $(MICROLITE_TEST_HDRS) $(3),$(LDFLAGS) $(GENERATED_PROJECT_LIBS),$(CXXFLAGS) $(GENERATED_PROJECT_INCLUDES), $(CCFLAGS) $(GENERATED_PROJECT_INCLUDES))
$(call generate_ceva_sp500_project,make,$(MAKE_PROJECT_FILES) $($(1)_MAKE_PROJECT_FILES),$(1),$(MICROLITE_CC_SRCS) $(THIRD_PARTY_CC_SRCS) $(MICROLITE_CC_KERNEL_SRCS) $(2),$(MICROLITE_CC_HDRS) $(THIRD_PARTY_CC_HDRS) $(MICROLITE_TEST_HDRS) $(3),$(LDFLAGS) $(GENERATED_PROJECT_LIBS),$(CXXFLAGS) $(GENERATED_PROJECT_INCLUDES), $(CCFLAGS) $(GENERATED_PROJECT_INCLUDES))
$(call generate_project,keil,$(KEIL_PROJECT_FILES) $($(1)_KEIL_PROJECT_FILES),$(1),$(MICROLITE_CC_SRCS) $(THIRD_PARTY_CC_SRCS) $(MICROLITE_CC_KERNEL_SRCS) $(2),$(MICROLITE_CC_HDRS) $(THIRD_PARTY_CC_HDRS) $(MICROLITE_TEST_HDRS) $(3),$(MICROLITE_LIBS),$(CXXFLAGS),$(CCFLAGS),$(TARGET_TOOLCHAIN_ROOT),$(TARGET_TOOLCHAIN_PREFIX))
ifeq (,$(findstring _benchmark,$(1)))
  $(call generate_arduino_project,$(ARDUINO_PROJECT_FILES) $($(1)_ARDUINO_PROJECT_FILES),$(1),$(MICROLITE_CC_SRCS) $(THIRD_PARTY_CC_SRCS) $(MICROLITE_CC_KERNEL_SRCS) $(2),$(MICROLITE_CC_HDRS) $(THIRD_PARTY_CC_HDRS) $(MICROLITE_TEST_HDRS) $(3),$(MICROLITE_LIBS),$(CXXFLAGS),$(CCFLAGS))
endif
$(call generate_esp_project,$(ESP_PROJECT_FILES) $($(1)_ESP_PROJECT_FILES),$(1),$(MICROLITE_CC_SRCS) $(THIRD_PARTY_CC_SRCS) $(MICROLITE_CC_KERNEL_SRCS),$(MICROLITE_CC_HDRS) $(THIRD_PARTY_CC_HDRS) $(MICROLITE_TEST_HDRS),$(2),$(3),$(MICROLITE_LIBS),$(CXXFLAGS),$(CCFLAGS),$(PROJECT_INCLUDES))
endef

#Handles the details of calculating the size of a binary target.
#
#Arguments are:
# 1 - Name of target.
# 2 - Regular expression for symbols to remove from the size calculation.
#Calling eval on the output will create the targets that you need.
define microlite_size
size_$(1): $$($(1)_BINARY)
	$$(SIZE_SCRIPT) $$($(1)_BINARY) $2
endef

# Handles the details of generating a binary target, including specializing
# for the current platform, and generating project file targets.
#
# Note that while the function is called microlite_test, it is used for both
# test and non-test binaries.

# Files that end with _test are added as test targets (i.e. can be executed with
# make test_<target>. All others can be executed with make run_<target>
#
# Arguments are:
# 1 - Name of target.
# 2 - C/C++ source files
# 3 - C/C++ header files
# 4 - Model sources and model test inputs in.tflite, .wav or.bmp format.
# Calling eval on the output will create the targets that you need.
define microlite_test
ifeq (,$(findstring _test, $(1)))
	$(eval $(call generate_project_third_party_parsing))
endif

$(1)_LOCAL_SRCS := $(2)

ifneq ($(4),)
  # Generate cc files and headers for all models and bitmaps in the test.
  GEN_RESULT := $$(shell python3 $(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/generate_cc_arrays.py $$(GENERATED_SRCS_DIR) $(4))

# The first ifneq is needed to be compatible with make versions prior to 4.2
  # which do not support .SHELLSTATUS. While make 4.2 was released in 2016,
  # Ubuntu 18.04 only has version 4.1
  ifneq ($(.SHELLSTATUS),)
    ifneq ($$(.SHELLSTATUS),0)
      $$(error Something went wrong: $$(GEN_RESULT))
    endif
  endif

  $(1)_LOCAL_SRCS += $$(GEN_RESULT)
endif

$(1)_LOCAL_SRCS := $$(call specialize,$$($(1)_LOCAL_SRCS))
ALL_SRCS += $$($(1)_LOCAL_SRCS)
$(1)_LOCAL_HDRS := $(3)
$(1)_LOCAL_OBJS := $$(addprefix $$(CORE_OBJDIR), \
$$(patsubst %.S,%.o,$$(patsubst %.cc,%.o,$$(patsubst %.c,%.o,$$($(1)_LOCAL_SRCS)))))
$(1)_BINARY := $$(BINDIR)$(1)
$$($(1)_BINARY): $$($(1)_LOCAL_OBJS) $$(MICROLITE_LIB_PATH)
	@mkdir -p $$(dir $$@)
	$$(CXX) $$(CXXFLAGS) $$(INCLUDES) \
	-o $$($(1)_BINARY) $$($(1)_LOCAL_OBJS) \
	$$(MICROLITE_LIB_PATH) $$(LDFLAGS) $$(MICROLITE_LIBS)
$(1): $$($(1)_BINARY)
$(1)_bin: $$($(1)_BINARY).bin

MICROLITE_BUILD_TARGETS += $$($(1)_BINARY)

ifneq (,$(findstring _test,$(1)))
  MICROLITE_TEST_TARGETS += test_$(1)

test_$(1): $$($(1)_BINARY)
	$$(TEST_SCRIPT) $$($(1)_BINARY) $$(TEST_PASS_STRING) $$(TARGET)

else
run_$(1): $$($(1)_BINARY)
	$$(TEST_SCRIPT) $$($(1)_BINARY) non_test_binary $$(TARGET)
endif

$(eval $(call generate_microlite_projects,$(1),$(call specialize,$(2)),$(3)))
endef

# Adds a dependency for a third-party library that needs to be downloaded from
# an external source.
# Arguments are:
# 1 - URL to download archive file from (can be .zip, .tgz, or .bz).
# 2 - MD5 sum of archive, to check integrity. Use md5sum tool to generate.
# 3 - Folder name to unpack library into, inside tf/l/x/m/t/downloads root.
# 4 - Optional patching action, must match clause in download_and_extract.sh.
# 5 - Optional patching action parameter
# These arguments are packed into a single '!' separated string, so no element
# can contain a '!'.
define add_third_party_download
THIRD_PARTY_DOWNLOADS += $(1)!$(2)!$(TENSORFLOW_ROOT)tensorflow/lite/micro/tools/make/downloads/$(3)!$(4)!$(5)
endef

# Unpacks an entry in a list of strings created by add_third_party_download, and
# defines a dependency rule to download the library. The download_and_extract.sh
# script is used to handle to downloading and unpacking.
# 1 - Information about the library, separated by '!'s.
define create_download_rule
$(word 3, $(subst !, ,$(1))):
	$(DOWNLOAD_SCRIPT) $(subst !, ,$(1))
THIRD_PARTY_TARGETS += $(word 3, $(subst !, ,$(1)))
endef

# Recursively find all files of given pattern
# Arguments are:
# 1 - Starting path
# 2 - File pattern, e.g: *.h
recursive_find = $(wildcard $(1)$(2)) $(foreach dir,$(wildcard $(1)*),$(call recursive_find,$(dir)/,$(2)))


# Modifies the Makefile to include all third party Srcs so that generate
# projects will create a Makefile that can be immediatley compiled without
# modification
define generate_project_third_party_parsing

ifeq ($$(PARSE_THIRD_PARTY), true)
# Get generated src includes with update path to third party
THIRD_PARTY_CC_SRCS += $$(filter $$(MAKEFILE_DIR)/downloads/%, $$(MICROLITE_CC_SRCS))
MICROLITE_CC_SRCS := $$(filter-out $$(THIRD_PARTY_CC_SRCS), $$(MICROLITE_CC_SRCS))
THIRD_PARTY_CC_SRCS :=  $$(sort $$(patsubst  $$(MAKEFILE_DIR)/downloads/%, third_party/%,  $$(THIRD_PARTY_CC_SRCS)))

# Get generated project includes from the includes with update path to third_party
GENERATED_PROJECT_INCLUDES += $$(filter -I$$(MAKEFILE_DIR)/downloads/%, $$(INCLUDES))
GENERATED_PROJECT_INCLUDES := $$(patsubst  -I$$(MAKEFILE_DIR)/downloads/%, -Ithird_party/%,  $$(GENERATED_PROJECT_INCLUDES))
GENERATED_PROJECT_INCLUDES += $$(filter -isystem$$(MAKEFILE_DIR)/downloads/%, $$(INCLUDES))
GENERATED_PROJECT_INCLUDES := $$(sort $$(patsubst  -isystem$$(MAKEFILE_DIR)/downloads/%, -isystemthird_party/%,  $$(GENERATED_PROJECT_INCLUDES)))

# We dont copy the libraries, we just want to make sure we link to them correctly.
MICROLITE_LIBS :=  $$(sort $$(patsubst  $$(MAKEFILE_DIR)/downloads/%, $$(TENSORFLOW_ROOT)$$(MAKEFILE_DIR)/downloads/%, $$(MICROLITE_LIBS)))
LDFLAGS :=   $$(sort $$(patsubst  $$(MAKEFILE_DIR)/downloads/%, $$(TENSORFLOW_ROOT)$$(MAKEFILE_DIR)/downloads/%, $$(LDFLAGS)))

# Copy all third party headers that are mentioned in includes
THIRD_PARTY_CC_HDRS += $$(filter $$(MAKEFILE_DIR)/downloads/%, $$(MICROLITE_CC_HDRS))
MICROLITE_CC_HDRS:= $$(sort $$(filter-out $$(THIRD_PARTY_CC_HDRS), $$(MICROLITE_CC_HDRS)))
THIRD_PARTY_CC_HDRS :=  $$(sort $$(patsubst  $$(MAKEFILE_DIR)/downloads/%, third_party/%,  $$(THIRD_PARTY_CC_HDRS)))

# Copy all third party headers that are mentioned in includes
INCLUDE_HDRS := $$(wildcard $$(addsuffix /*.h,$$(filter $$(MAKEFILE_DIR)/downloads/%, $$(patsubst -I%,%,$$(INCLUDES)))))
INCLUDE_HDRS += $$(wildcard $$(addsuffix /*.h,$$(filter $$(MAKEFILE_DIR)/downloads/%,  $$(patsubst -isystem%,%,$$(INCLUDES)))))
INCLUDE_HDRS :=  $$(sort $$(INCLUDE_HDRS))
THIRD_PARTY_CC_HDRS += $ $$(sort $(patsubst  $$(MAKEFILE_DIR)/downloads/%, third_party/%,  $$(INCLUDE_HDRS)))
endif

endef
