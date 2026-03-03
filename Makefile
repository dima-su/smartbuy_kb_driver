obj-m += ldp.o 
all:
	make -C /lib/modules/6.18.9-arch1-2/build/ M=$(PWD) modules
clean:
	make -C /lib/modules/6.18.9-arch1-2/build/ M=$(PWD) clean

