savedcmd_ldp.ko := ld -r -m elf_x86_64 -z noexecstack --no-warn-rwx-segments --build-id=sha1  -T /usr/lib/modules/6.18.9-arch1-2/build/scripts/module.lds -o ldp.ko ldp.o ldp.mod.o .module-common.o
