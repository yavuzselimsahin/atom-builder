CC      ?= cc
CFLAGS  ?= -O2
LDFLAGS ?=

# `override` so that `make CFLAGS="-O1 -g -fsanitize=address"` keeps the
# include paths and dependency generation instead of dropping them.
WARNINGS         := -Wall -Wextra
override CFLAGS  += $(WARNINGS) -I./include -MMD -MP

SRCS := src/main.c src/util.c src/manifest.c src/exec.c src/binfmt.c \
        src/build.c src/hash.c src/archive.c src/package.c src/json.c \
        src/publish.c src/publish_ssh.c src/publish_gh.c src/verify.c \
        src/cache.c src/driver_container.c src/buildsys.c src/init.c toml.c

BIN  := atom

OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(BIN)
	./tests/run.sh

# Installs into $(PREFIX)/bin. The default follows convention and needs sudo;
# `make install PREFIX=~/.local` puts it somewhere already on most PATHs
# without one.
PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin

install: $(BIN)
	mkdir -p $(BINDIR)
	cp -p $(BIN) $(BINDIR)/$(BIN)
	@echo "installed $(BINDIR)/$(BIN)"

# For working on atom itself: a symlink means a rebuild is live immediately,
# with no second step to forget.
link: $(BIN)
	mkdir -p $(BINDIR)
	ln -sf $(CURDIR)/$(BIN) $(BINDIR)/$(BIN)
	@echo "linked $(BINDIR)/$(BIN) -> $(CURDIR)/$(BIN)"

uninstall:
	rm -f $(BINDIR)/$(BIN)

clean:
	rm -f $(OBJS) $(DEPS) $(BIN)

-include $(DEPS)

.PHONY: all clean test install link uninstall
