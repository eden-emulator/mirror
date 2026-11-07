These certificates and private keys are generated test data for loopback TLS only.
`cert.pem` is the explicitly trusted certificate; `untrusted-cert.pem` is an
independent self-signed certificate used to prove verification rejects an
untrusted server. Both certificates name `localhost` and expire in October 2036.
No production credentials are used.

Each pair was generated with OpenSSL:

```sh
openssl req -x509 -newkey rsa:2048 -noenc -days 3650 -subj /CN=localhost \
  -addext subjectAltName=DNS:localhost -addext basicConstraints=critical,CA:TRUE \
  -keyout key.pem -out cert.pem
```

Run the command again with the `untrusted-` filenames to create the independent
negative-test certificate. Replace these fixtures before their expiry when
running the suite after 2036.
