# ---------------------------------------------------------------------------
# platform specific
# ---------------------------------------------------------------------------
ifndef DEV
CC	        = arm-linux-gcc
CXX	        = arm-linux-g++
LDFLAGS     = -m32 -pthread -lrt
CFLAGS      = -g -pthread -std=gnu99

else

CC	        = gcc
CXX	        = g++
LDFLAGS     += -m32 -pthread
CFLAGS      += -m32 -Wall -g -pthread -std=gnu99
endif


# ---------------------------------------------------------------------------
# project specifics
# ---------------------------------------------------------------------------
TGT_SRV      = hcomm_demo_server
CSRC_SRV     = hcomm_demo_server.c \
			   hserver.c \
               hcomm.c		  

OBJS_SRV        = $(CSRC_SRV:.c=.o)
DEPS_SRV        = $(OBJS_SRV:.o=.d) $(NOLINK_OBJS_SRV:.o=.d)
BIN_SRV         = $(TGT_SRV)

TGT_CLI = hcomm_demo_client
CSRC_CLI = hcomm_demo_client.c \
			hcomm.c \
			hclient.c

OBJS_CLI        = $(CSRC_CLI:.c=.o)
DEPS_CLI        = $(OBJS_CLI:.o=.d) $(NOLINK_OBJS_CLI:.o=.d)
BIN_CLI         = $(TGT_CLI)

.PHONY: clean all

all: $(BIN_SRV) $(BIN_CLI)

$(BIN_SRV): $(OBJS_SRV) $(NOLINK_OBJS_SRV)
	$(CC) $(LDFLAGS) $(OBJS_SRV) $(LDLIBS_SRV) -o $@

$(BIN_CLI): $(OBJS_CLI) $(NOLINK_OBJS_CLI)
	$(CC) $(LDFLAGS) $(OBJS_CLI) $(LDLIBS_CLI) -o $@

clean:
	rm -f $(DEPS_CLI)
	rm -f $(OBJS_CLI) $(NOLINK_OBJS_CLI)
	rm -f $(BIN_CLI)
	rm -f $(DEPS_SRV)
	rm -f $(OBJS_SRV) $(NOLINK_OBJS_CLI)
	rm -f $(BIN_SRV)

# ---------------------------------------------------------------------------
# rules for code generation
# ---------------------------------------------------------------------------
%.o:    %.c
	$(CC) $(CFLAGS) -o $@ -c $<