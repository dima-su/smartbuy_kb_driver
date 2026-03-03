savedcmd_ldp.mod := printf '%s\n'   ldp.o | awk '!x[$$0]++ { print("./"$$0) }' > ldp.mod
