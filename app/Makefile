# ─────────────────────────────────────────────────────────────────────────────
# Weather ACAP — ACAP Native SDK Makefile
#
# The SDK environment-setup-* script sets CC, CFLAGS, LDFLAGS, PKG_CONFIG_PATH.
# acap-build calls 'make', then packages:
#   - the binary named <appName> (weather_acap)
#   - the html/ directory (static files + compiled CGI binary)
# ─────────────────────────────────────────────────────────────────────────────

MAIN_PROG = weather_acap
CGI_PROG  = html/config_cgi   # built directly into html/ so acap-build includes it

PKGS    = glib-2.0 axparameter libcurl
CFLAGS += $(shell pkg-config --cflags $(PKGS)) \
          -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
LDLIBS  = $(shell pkg-config --libs $(PKGS)) -lm

# Main daemon sources
MAIN_SRCS = weather_acap.c nws.c openmeteo.c weather_api.c \
            alerts.c overlay.c params.c cJSON.c
MAIN_OBJS = $(MAIN_SRCS:.c=.o)

# CGI sources (shares params.o and cJSON.o)
CGI_SRCS = config_cgi.c

.PHONY: all clean

all: $(MAIN_PROG) $(CGI_PROG)

# Main daemon
$(MAIN_PROG): $(MAIN_OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

# Config CGI — built into html/ and shares prebuilt params.o + cJSON.o
$(CGI_PROG): config_cgi.o params.o cJSON.o
	$(CC) -o $@ $^ $(LDLIBS)

$(CGI_PROG): | html

html:
	mkdir -p html

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) *.o $(MAIN_PROG) html/config_cgi
