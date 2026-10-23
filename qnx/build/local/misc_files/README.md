# misc_files/ - guest-image credentials (local only)

The IFS build bakes a small set of credentials into the QNX guest image:
SSH host keys, an ssh known-hosts list (`ssh-ident`), and a root password
hash (`shadow`). These are image-local secrets.

Everything in this directory except this README and `gen_guest_keys.sh` is
gitignored. Generate your own set before running `qnx/build/build-ifs.sh`:

```bash
bash qnx/build/local/misc_files/gen_guest_keys.sh
```

Never commit these files, and never distribute a guest image that contains
them. QNX/BSP-derived build products (startup binary, ifs.bin, disk image)
are likewise not redistributable - each user builds with their own SDP 8.0.
