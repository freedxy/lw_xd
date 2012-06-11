
UPNP_DIR=/usr/local
UPNP_INC=$(UPNP_DIR)/include/upnp/
UPNP_LIB=$(UPNP_DIR)/lib/

all:
	gcc -I$(UPNP_INC) -L$(UPNP_LIB) \
	-I./ -Ilinux/ -I../common/ \
	upnp_tv_ctrlpt.c \
	linux/upnp_tv_ctrlpt_main.c \
	../common/sample_util.c \
	-o tv_ctrlpt \
	-lixml -lthreadutil -lupnp -g

clean:
	rm -rf tv_ctrlpt
