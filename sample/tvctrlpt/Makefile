
UPNP_DIR=/home/wangli/MyProject/test_upnp/libupnp-1.6.6/upnp_tmp
UPNP_INC=$(UPNP_DIR)/include/upnp/
UPNP_LIB=$(UPNP_DIR)/lib/

all:
	gcc -I$(UPNP_INC) -L$(UPNP_LIB) \
	-I./ -Ilinux/ -I../common/ \
	upnp_tv_ctrlpt.c \
	linux/upnp_tv_ctrlpt_main.c \
	../common/sample_util.c \
	-o tv_ctrlpt \
	-lixml -lthreadutil -lupnp

clean:
	rm -rf tv_ctrlpt
