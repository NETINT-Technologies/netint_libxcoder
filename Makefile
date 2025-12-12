.PHONY: all default test clean cleanall install uninstall
WINDOWS ?= FALSE
OPENHARMONY ?= FALSE
GDB ?= FALSE
WARN_AS_ERROR ?= FALSE
SECURE_COMPILE ?= FALSE
RPM_DEBUG ?= FALSE
DONT_WRITE_SONAME ?= FALSE
SETUP_SYSTEMD ?= FALSE
DEPRECATION_AS_ERROR ?= FALSE
CPU_AFFINITY ?= FALSE

C_STANDARD = -std=gnu99
CXX_STANDARD = -std=c++11
INSTALL = install
COPY_CMD = cp
UNAME = $(shell uname)

ifeq ($(OPENHARMONY), TRUE)
	CC = ${OHOS_SDK_ROOT}/linux/native/llvm/bin/aarch64-unknown-linux-ohos-clang
	CXX = ${OHOS_SDK_ROOT}/linux/native/llvm/bin/aarch64-unknown-linux-ohos-clang++
endif

ifeq ($(RPM_DEBUG), TRUE)
	CFLAGS = -g -O3
else
	ifeq ($(GDB), TRUE)
		CFLAGS = -g
	else
		CFLAGS = -O3
		ifeq ($(WINDOWS), FALSE)
		ifneq ($(UNAME), Darwin)
			LDFLAGS = -Wl,-strip-all
		endif
		endif
	endif
endif

ifeq ($(SECURE_COMPILE), TRUE)
	CFLAGS += -Wall -fstack-protector-strong
	LDFLAGS += -Wl,-z,now -Wl,-z,relro -Wl,-z,noexecstack
endif

ifeq ($(CPU_AFFINITY), TRUE)
	LDFLAGS += -lnuma
endif

ifeq ($(UNAME), Darwin)
	WERROR_FLAGS = -Werror -Wno-unused-command-line-argument
else
	ifeq ($(WARN_AS_ERROR), TRUE)
		WERROR_FLAGS = -Werror -DNI_WARN_AS_ERROR
	else
		WERROR_FLAGS = -UNI_WARN_AS_ERROR
	endif
endif

CFLAGS += -fPIC ${WERROR_FLAGS} -DLIBXCODER_OBJS_BUILD
ifeq ($(OPENHARMONY), TRUE)
	CFLAGS += -D__OPENHARMONY__
endif

ifeq ($(DEPRECATION_AS_ERROR), TRUE)
	CFLAGS += -DDEPRECATION_AS_ERROR
endif

TARGETNAME = xcoder
TARGETP2P = xcoderp2p
TARGETP2PREAD = xcoderp2p_read
TARGET_LIB = lib${TARGETNAME}.a
TARGET_LIB_SHARED = lib${TARGETNAME}.so
TARGET_VERSION = $(shell grep 'Version: '.* < build/xcoder.pc  | cut -d ' ' -f 2)
ifeq ($(WINDOWS), FALSE)
	TARGET_INCS = ni_device_api.h ni_rsrc_api.h ni_defs.h ni_av_codec.h ni_bitstream.h ni_util.h ni_log.h ni_release_info.h ni_libxcoder_dynamic_loading.h ni_p2p_ioctl.h ni_quadraprobe.h
else
	TARGET_INCS = ni_device_api.h ni_rsrc_api.h ni_defs.h ni_av_codec.h ni_bitstream.h ni_util.h ni_log.h ni_release_info.h
endif
TARGET_PC = xcoder.pc
SERVICE_FILE = nilibxcoder.service
OBJECTS = ni_nvme.o ni_device_api_priv.o ni_device_api.o ni_util.o ni_lat_meas.o ni_log.o ni_rsrc_priv.o ni_rsrc_api.o ni_av_codec.o ni_bitstream.o
LINK_OBJECTS = ${OBJS_PATH}/ni_nvme.o ${OBJS_PATH}/ni_device_api_priv.o ${OBJS_PATH}/ni_device_api.o ${OBJS_PATH}/ni_util.o ${OBJS_PATH}/ni_lat_meas.o ${OBJS_PATH}/ni_log.o ${OBJS_PATH}/ni_rsrc_priv.o ${OBJS_PATH}/ni_rsrc_api.o ${OBJS_PATH}/ni_av_codec.o ${OBJS_PATH}/ni_bitstream.o
ifeq ($(WINDOWS), FALSE)
ifneq ($(UNAME), Darwin)
    OBJECTS += ni_quadraprobe.o
    LINK_OBJECTS += ${OBJS_PATH}/ni_quadraprobe.o
endif
endif
DEMO_OBJECTS = ni_xcoder_decode.o ni_xcoder_encode.o ni_xcoder_scale.o ni_xcoder_transcode_filter.o ni_xcoder_multithread_transcode.o ni_generic_utils.o ni_decode_utils.o ni_encode_utils.o ni_filter_utils.o
DEMO_LINK_OBJECTS = ${OBJS_PATH}/ni_generic_utils.o ${OBJS_PATH}/ni_decode_utils.o ${OBJS_PATH}/ni_encode_utils.o ${OBJS_PATH}/ni_filter_utils.o 
ALL_OBJECTS = init_rsrc.o test_rsrc_api.o ni_rsrc_mon.o ni_rsrc_update.o ni_rsrc_list.o ni_rsrc_namespace.o ${OBJECTS} ${DEMO_OBJECTS}
ifeq ($(WINDOWS), FALSE)
	ifneq ($(UNAME), Darwin)
		ALL_OBJECTS += ni_p2p_test.o ni_p2p_read_test.o ni_libxcoder_dynamic_loading_test.o
	endif
endif

SRC_PATH = ./source
OBJS_PATH = ./build

# Read the installation directory from path set in build/xcoder.pc
# DESTDIR ?= $(shell sed -n 's/^prefix=\(.*\)/\1/p' $(OBJS_PATH)/$(TARGET_PC))
LIBDIR_NO_PREFIX = $(shell sed -n 's/^libdir=\(.*\)/\1/p' $(OBJS_PATH)/$(TARGET_PC))
BINDIR_NO_PREFIX = $(shell sed -n 's/^bindir=\(.*\)/\1/p' $(OBJS_PATH)/$(TARGET_PC))
INCLUDEDIR_NO_PREFIX = $(shell sed -n 's/^includedir=\(.*\)/\1/p' $(OBJS_PATH)/$(TARGET_PC))
SHAREDDIR_NO_PREFIX = $(shell sed -n 's/^shareddir=\(.*\)/\1/p' $(OBJS_PATH)/$(TARGET_PC))
SYSTEMDIR_NO_PREFIX = $(shell sed -n 's/^systemdir=\(.*\)/\1/p' $(OBJS_PATH)/$(TARGET_PC))
ifeq ($(WINDOWS), FALSE)
	ifeq ($(UNAME), Darwin)
		LDFLAGS += -lpthread -lm
	else
		LDFLAGS += -lpthread -lrt -lm -ldl
	endif
else
	LDFLAGS += -lws2_32 -Wl,--stack,4194304
endif

LIBDIR = ${LIBDIR_NO_PREFIX}
BINDIR = ${BINDIR_NO_PREFIX}
INCLUDEDIR = ${INCLUDEDIR_NO_PREFIX}
SHAREDDIR = ${SHAREDDIR_NO_PREFIX}
SYSTEMDIR = ${SYSTEMDIR_NO_PREFIX}

ifdef DESTDIR
	LIBDIR = ${DESTDIR}${LIBDIR_NO_PREFIX}
	BINDIR = ${DESTDIR}${BINDIR_NO_PREFIX}
	INCLUDEDIR = ${DESTDIR}${INCLUDEDIR_NO_PREFIX}
	ifneq (${SHAREDDIR_NO_PREFIX},)
		SHAREDDIR = ${DESTDIR}${SHAREDDIR_NO_PREFIX}
	endif
	ifneq (${SYSTEMDIR_NO_PREFIX},)
		SYSTEMDIR = ${DESTDIR}${SYSTEMDIR_NO_PREFIX}
	endif
endif

# make target
all:${ALL_OBJECTS}
	ar rcs $(OBJS_PATH)/${TARGET_LIB} ${LINK_OBJECTS}
ifeq ($(UNAME), Darwin)
	${CC} -shared -o $(OBJS_PATH)/${TARGET_LIB_SHARED}.${TARGET_VERSION} $(LINK_OBJECTS) ${LDFLAGS}
else ifeq ($(DONT_WRITE_SONAME), TRUE)
	${CC} -shared -o $(OBJS_PATH)/${TARGET_LIB_SHARED}.${TARGET_VERSION} $(LINK_OBJECTS) ${LDFLAGS}
else
	${CC} -shared -Wl,-soname,${TARGET_LIB_SHARED}.${TARGET_VERSION} -o $(OBJS_PATH)/${TARGET_LIB_SHARED}.${TARGET_VERSION} $(LINK_OBJECTS) ${LDFLAGS}
endif

	${CC} -o $(OBJS_PATH)/ni_xcoder_decode $(OBJS_PATH)/ni_xcoder_decode.o $(LINK_OBJECTS) ${DEMO_LINK_OBJECTS} ${LDFLAGS}
	${CC} -o $(OBJS_PATH)/ni_xcoder_encode $(OBJS_PATH)/ni_xcoder_encode.o $(LINK_OBJECTS) ${DEMO_LINK_OBJECTS} ${LDFLAGS}
	${CC} -o $(OBJS_PATH)/ni_xcoder_scale $(OBJS_PATH)/ni_xcoder_scale.o $(LINK_OBJECTS) ${DEMO_LINK_OBJECTS} ${LDFLAGS}
	${CC} -o $(OBJS_PATH)/ni_xcoder_transcode_filter $(OBJS_PATH)/ni_xcoder_transcode_filter.o $(LINK_OBJECTS) ${DEMO_LINK_OBJECTS} ${LDFLAGS}
	${CC} -o $(OBJS_PATH)/ni_xcoder_multithread_transcode $(OBJS_PATH)/ni_xcoder_multithread_transcode.o $(LINK_OBJECTS) ${DEMO_LINK_OBJECTS} ${LDFLAGS}

ifeq ($(WINDOWS), FALSE)
ifeq ($(OPENHARMONY), FALSE)
ifneq ($(UNAME), Darwin)
	${CC} -o $(OBJS_PATH)/ni_libxcoder_dynamic_loading_test $(OBJS_PATH)/ni_libxcoder_dynamic_loading_test.o $(LINK_OBJECTS) ${LDFLAGS}
	${CC} -o $(OBJS_PATH)/${TARGETP2P} $(OBJS_PATH)/ni_p2p_test.o $(LINK_OBJECTS) ${LDFLAGS}
	${CC} -o $(OBJS_PATH)/${TARGETP2PREAD} $(OBJS_PATH)/ni_p2p_read_test.o $(LINK_OBJECTS) ${LDFLAGS}
endif
endif
endif

	${CC} -o $(OBJS_PATH)/ni_rsrc_mon $(OBJS_PATH)/ni_rsrc_mon.o $(LINK_OBJECTS) ${LDFLAGS}
	${CC} -o $(OBJS_PATH)/test_rsrc_api $(OBJS_PATH)/test_rsrc_api.o $(LINK_OBJECTS) ${LDFLAGS}
	${CC} -o $(OBJS_PATH)/ni_rsrc_namespace $(OBJS_PATH)/ni_rsrc_namespace.o $(LINK_OBJECTS) ${LDFLAGS}
	${CC} -o $(OBJS_PATH)/ni_rsrc_update $(OBJS_PATH)/ni_rsrc_update.o $(LINK_OBJECTS) ${LDFLAGS}
	${CC} -o $(OBJS_PATH)/ni_rsrc_list $(OBJS_PATH)/ni_rsrc_list.o $(LINK_OBJECTS) ${LDFLAGS}
	${CC} -o $(OBJS_PATH)/init_rsrc $(OBJS_PATH)/init_rsrc.o $(LINK_OBJECTS) ${LDFLAGS}
	@echo info ${TARGET_LIB_SHARED}

install:
	mkdir -p ${INCLUDEDIR}/
	mkdir -p ${LIBDIR}/
	mkdir -p ${LIBDIR}/pkgconfig/
	mkdir -p ${BINDIR}/
	for TARGET_INC in ${TARGET_INCS}; do \
		${INSTALL} -m 644 ${SRC_PATH}/$${TARGET_INC} ${INCLUDEDIR}/.; \
	done
ifeq ($(RPM_DEBUG), TRUE)
	${INSTALL} -m 644 -s ${OBJS_PATH}/${TARGET_LIB} ${LIBDIR}/.
	${INSTALL} -m 644 -s ${OBJS_PATH}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${LIBDIR}/.
else
	${INSTALL} -m 644 ${OBJS_PATH}/${TARGET_LIB} ${LIBDIR}/.
	${INSTALL} -m 644 ${OBJS_PATH}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${LIBDIR}/.
endif
	(cd ${LIBDIR}; ln -f -s ${TARGET_LIB_SHARED}.${TARGET_VERSION} ${TARGET_LIB_SHARED}; cd -)
ifneq ($(SHAREDDIR),)
	mkdir -p ${SHAREDDIR}/
	${INSTALL} -m 644 ${OBJS_PATH}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${SHAREDDIR}/.
	(cd ${SHAREDDIR}; ln -f -s ${TARGET_LIB_SHARED}.${TARGET_VERSION} ${TARGET_LIB_SHARED}; cd -)
endif
	${INSTALL} -m 644 ${OBJS_PATH}/${TARGET_PC} ${LIBDIR}/pkgconfig/.
	${INSTALL} -m 755 ${OBJS_PATH}/ni_rsrc_mon ${BINDIR}/.
	${INSTALL} -m 755 ${OBJS_PATH}/ni_rsrc_namespace ${BINDIR}/.
	${INSTALL} -m 755 ${OBJS_PATH}/ni_rsrc_update ${BINDIR}/.
	${INSTALL} -m 755 ${OBJS_PATH}/ni_rsrc_list ${BINDIR}/.
	${INSTALL} -m 755 ${OBJS_PATH}/init_rsrc ${BINDIR}/.
	${INSTALL} -m 755 host_ts.sh ${BINDIR}/.
ifeq ($(SETUP_SYSTEMD), TRUE)
	mkdir -p ${SYSTEMDIR}/
	$(COPY_CMD) $(SERVICE_FILE) $(SYSTEMDIR)
	systemctl enable $(SERVICE_FILE)
	systemctl start $(SERVICE_FILE)
endif

uninstall:
	for TARGET_INC in ${TARGET_INCS}; do \
		rm -f ${INCLUDEDIR}/$${TARGET_INC}; \
	done
	rm -f ${LIBDIR}/${TARGET_LIB} ${LIBDIR}/${TARGET_LIB_SHARED} ${LIBDIR}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${LIBDIR}/pkgconfig/${TARGET_PC} ${BINDIR}/ni_rsrc_mon ${BINDIR}/ni_rsrc_update ${BINDIR}/ni_rsrc_list ${BINDIR}/init_rsrc ${BINDIR}/ni_rsrc_namespace
ifneq ($(SHAREDDIR),)
	rm -f ${SHAREDDIR}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${SHAREDDIR}/${TARGET_LIB_SHARED} 
endif
ifeq ($(shell [ -e $(SYSTEMDIR)/$(SERVICE_FILE) ] && echo yes || echo no), yes)
	systemctl disable $(SERVICE_FILE)
	systemctl stop $(SERVICE_FILE)
	rm $(SYSTEMDIR)/$(SERVICE_FILE)
	systemctl daemon-reload
endif

cleanall:clean
	rm -rf $(OBJS_PATH)/*${TARGETNAME}* $(OBJS_PATH)/*.o

clean:
	rm -rf $(OBJS_PATH)/*${TARGETNAME}* $(OBJS_PATH)/*.o

# dependence
%.o : ${SRC_PATH}/%.cpp
	${CXX} ${CFLAGS} ${CXX_STANDARD} -c $< -o ${OBJS_PATH}/$@

%.o : ${SRC_PATH}/%.c
	${CC} ${CFLAGS} ${C_STANDARD} -c $< -o ${OBJS_PATH}/$@

%.o : ${SRC_PATH}/examples/%.c
	${CC} ${CFLAGS} ${C_STANDARD} -I${SRC_PATH} -I${SRC_PATH}/examples/common -c $< -o ${OBJS_PATH}/$@

%.o : ${SRC_PATH}/examples/common/%.c
	${CC} ${CFLAGS} ${C_STANDARD} -I${SRC_PATH} -c $< -o ${OBJS_PATH}/$@
