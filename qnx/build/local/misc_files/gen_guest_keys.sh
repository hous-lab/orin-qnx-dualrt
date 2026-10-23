#!/usr/bin/env bash
# gen_guest_keys.sh - generate guest-image credentials locally (never commit these).
#
# Consumed by the IFS build under qnx/build/local/. Files produced here are
# gitignored image-local secrets; each user generates their own set before
# running build-ifs.sh.
set -euo pipefail
cd "$(cd "$(dirname "$0")" && pwd)"

[ -f ssh_host_ed25519_key ] || ssh-keygen -q -t ed25519 -N "" -f ssh_host_ed25519_key -C "guest host key"
[ -f ssh_host_rsa_key ]     || ssh-keygen -q -t rsa -b 3072 -N "" -f ssh_host_rsa_key -C "guest host key"
[ -f qtd_private_key.pem ]  || openssl genrsa -out qtd_private_key.pem 2048
[ -f qtd_public_key.pem ]   || openssl rsa -in qtd_private_key.pem -pubout -out qtd_public_key.pem
chmod 600 ssh_host_ed25519_key ssh_host_rsa_key qtd_private_key.pem

# known-hosts list used as: ssh -o UserKnownHostsFile=local/ssh-ident ...
ssh-keygen -y -f ssh_host_ed25519_key | sed 's/^/* /'  > ssh-ident
ssh-keygen -y -f ssh_host_rsa_key     | sed 's/^/* /' >> ssh-ident
chmod 644 ssh-ident

# root password hash for the guest /etc/shadow
if [ ! -f shadow ]; then
  read -r -s -p "guest root password: " pw; echo
  read -r -s -p "confirm: " pw2; echo
  [ "$pw" = "$pw2" ] || { echo "mismatch" >&2; exit 1; }
  printf 'root:%s:0:0:0:0:0:0:0:0:::\n' "$(openssl passwd -6 "$pw")" > shadow
  chmod 600 shadow
fi
echo "guest credentials ready in $(pwd)"
