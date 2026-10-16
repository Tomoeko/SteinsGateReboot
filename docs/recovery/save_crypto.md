# Save digest and cipher boundary

The portable first-party `core::Md5` reuses the existing archive cipher algorithm, with explicit binary input and little-endian digest bytes. It passes the independent [RFC1321](https://www.rfc-editor.org/rfc/rfc1321) sectionA.5 vectors, binary padding-boundary vectors, and chunked input checks; the archive stream's existing independent vectors remain unchanged.

The macOS save boundary uses CommonCrypto AES256-CBC, a zero16-byte IV and PKCS7 finalization. Native helpers `0x140553520`/`0x1405537d0` prepare the first32 C-string key bytes with zero fill and select those CryptoAPI parameters. Target executable SHA-256: `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

Eight independent system OpenSSL vectors cover empty, partial and aligned blocks; short,32-byte, overlong and NUL-containing key inputs. Decrypt requests raw CBC blocks from the OS and explicitly verifies every PKCS7 byte before returning the unpadded result. This preserves a checked final-padding contract; the original [CryptDecrypt](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptdecrypt) reports invalid padding as bad data. Empty/misaligned ciphertext and malformed padding reject.

These checks validate the digest/cipher primitives and OS boundary, not an original saved-game round trip. Native metadata framing, exact Struct publication and original save-file differential checks remain separate acceptance work. The game supplies its format's fixed IV/key preparation; no format replacement is introduced.
