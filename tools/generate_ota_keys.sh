#!/bin/sh
set -eu

OUT_DIR="${1:-ota_keys}"
mkdir -p "$OUT_DIR"
umask 077

PRIVATE_KEY="$OUT_DIR/ota_private_key.pem"
PUBLIC_KEY="$OUT_DIR/ota_public_key.pem"

if [ -e "$PRIVATE_KEY" ] || [ -e "$PUBLIC_KEY" ]; then
    echo "Refusing to overwrite existing keys in $OUT_DIR" >&2
    exit 1
fi

openssl genpkey -algorithm RSA \
    -pkeyopt rsa_keygen_bits:3072 \
    -out "$PRIVATE_KEY"

openssl pkey -in "$PRIVATE_KEY" -pubout -out "$PUBLIC_KEY"

chmod 600 "$PRIVATE_KEY"
chmod 644 "$PUBLIC_KEY"

echo "Private key: $PRIVATE_KEY"
echo "Public key : $PUBLIC_KEY"
echo
echo "IMPORTANT: keep the private key only on the release/signing machine."
echo "Deploy only the public key to /oem/keys/ota_public_key.pem on RK3506."
