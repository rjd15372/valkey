uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
CLANG := $(findstring clang,$(shell sh -c '$(CC) --version | head -1'))

DEPS_DIR=../../../deps

ifeq ($(uname_S),Darwin)
	SHOBJ_CFLAGS= -I. -I$(DEPS_DIR)/lua/src -I$(DEPS_DIR)/fpconv -fPIC -W -Wall -dynamic -fno-common $(OPTIMIZATION) -std=gnu11 -D_GNU_SOURCE $(CFLAGS)
	SHOBJ_LDFLAGS= -bundle -undefined dynamic_lookup $(LDFLAGS)
else
	SHOBJ_CFLAGS= -I. -I$(DEPS_DIR)/lua/src -I$(DEPS_DIR)/fpconv -fPIC -W -Wall -fno-common $(OPTIMIZATION) -std=gnu11 -D_GNU_SOURCE $(CFLAGS)
	SHOBJ_LDFLAGS= -shared $(LDFLAGS)

	SHOBJ_CFLAGS_5_4= -I. -I$(DEPS_DIR)/lua-5.4.8/src -I$(DEPS_DIR)/fpconv -fPIC -W -Wall -fno-common $(OPTIMIZATION) -std=c99 -D_GNU_SOURCE -DLUA_5_4 $(CFLAGS)

ifeq (clang,$(CLANG))
	SHOBJ_LDFLAGS+= -fuse-ld=lld
	SHOBJ_LDFLAGS_5_4+= -fuse-ld=lld
endif

endif

# BSD variants support
ifeq ($(uname_S),FreeBSD)
	SHOBJ_LDFLAGS+= -lm
else ifeq ($(uname_S),OpenBSD)
	SHOBJ_LDFLAGS+= -lm
else ifeq ($(uname_S),NetBSD)
	SHOBJ_LDFLAGS+= -lm
else ifeq ($(uname_S),DragonFly)
	SHOBJ_LDFLAGS+= -lm
endif

LIBS= $(DEPS_DIR)/lua/src/liblua.a $(DEPS_DIR)/fpconv/libfpconv.a
LIBS_5_4= $(DEPS_DIR)/lua-5.4.8/src/liblua.a $(DEPS_DIR)/fpconv/libfpconv.a
SRCS= $(wildcard *.c)
OBJS= $(SRCS:.c=.o) sha1.o rand.o
OBJS_5_4 = $(SRCS:.c=.5.4.o)

# OS X 11.x doesn't have /usr/lib/libSystem.dylib and needs an explicit setting.
ifeq ($(uname_S),Darwin)
ifeq ("$(wildcard /usr/lib/libSystem.dylib)","")
	SHOBJ_LDFLAGS+= -L /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib -lsystem
endif
endif

all: libvalkeylua.so libvalkeylua_5.4.so

libvalkeylua.so: $(OBJS) $(LIBS)
	$(CC) -o $@ $(SHOBJ_LDFLAGS) $^

sha1.o: ../../sha1.c
	$(CC) $(SHOBJ_CFLAGS) -c $< -o $@

rand.o: ../../rand.c
	$(CC) $(SHOBJ_CFLAGS) -c $< -o $@

libvalkeylua_5.4.so: $(LIBS_5_4) $(OBJS_5_4)
	$(LD) -o $@ $^ $(SHOBJ_LDFLAGS) $(LIBS_5_4)

%.o: %.c
	$(CC) $(SHOBJ_CFLAGS) -c $< -o $@

%.5.4.o: %.c
	$(CC) $(SHOBJ_CFLAGS_5_4) -c $< -o $@

$(DEPS_DIR)/lua/src/liblua.a:
	cd $(DEPS_DIR) && $(MAKE) lua

$(DEPS_DIR)/lua-5.4.8/src/liblua.a:
	cd $(DEPS_DIR) && $(MAKE) lua-5.4

$(DEPS_DIR)/fpconv/libfpconv.a:
	cd $(DEPS_DIR) && $(MAKE) fpconv

clean:
	rm -f *.so $(OBJS) $(OBJS_5_4)
