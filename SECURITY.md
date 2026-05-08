# Security Policy

## Reporting a vulnerability

If you discover a security issue in this SDK, please email **api@audd.io** privately. Do not open a public GitHub issue for security reports.

We will acknowledge receipt within 2 business days and coordinate disclosure with you.

## Scope

- **In scope:** vulnerabilities in this SDK's source code, including the vendored copies of cJSON and Unity.
- **Out of scope:** issues in upstream dependencies linked dynamically (libcurl, OpenSSL); file those with the upstream maintainer. Issues in the AudD service or API itself: email **api@audd.io** with subject `AudD service: <summary>`.

## Hardening practices

This SDK never logs `api_token`, request bodies, or response bodies. The token lives only in the client handle and travels with each request as a multipart form field — never on the URL line, never in environment exports the SDK initiates, never in error messages.
