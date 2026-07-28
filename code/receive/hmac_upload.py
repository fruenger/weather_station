"""HMAC-v1 helpers for the legacy Windows/Linux receive uploader."""

from __future__ import annotations

import hashlib
import hmac
import secrets
import time
from typing import Mapping, MutableMapping, Tuple
from urllib.parse import urlencode

PROTOCOL = 'WEATHER-HMAC-V1'
CONTENT_TYPE = 'application/x-www-form-urlencoded'


def encode_form(data: Mapping) -> bytes:
    return urlencode(list(data.items()), doseq=False).encode('utf-8')


def parse_secret_hex(secret_hex: str) -> bytes:
    cleaned = (secret_hex or '').strip().replace('\r', '').replace('\n', '')
    if len(cleaned) != 64:
        raise ValueError('hmac_secret_hex must be 64 hex characters')
    return bytes.fromhex(cleaned)


def sign_request(
    secret: bytes,
    *,
    device_id: str,
    key_id: str,
    body: bytes,
    canonical_path: str,
    timestamp: str | None = None,
    nonce: str | None = None,
) -> Tuple[bytes, MutableMapping[str, str]]:
    ts = timestamp if timestamp is not None else str(int(time.time()))
    nonce_hex = nonce if nonce is not None else secrets.token_hex(16)
    body_digest = hashlib.sha256(body).hexdigest()
    canonical = '\n'.join([
        PROTOCOL,
        'POST',
        canonical_path,
        CONTENT_TYPE,
        device_id,
        key_id,
        ts,
        nonce_hex,
        body_digest,
    ])
    signature = hmac.new(secret, canonical.encode('utf-8'), hashlib.sha256).hexdigest()
    headers = {
        'Content-Type': CONTENT_TYPE,
        'X-Weather-Device': device_id,
        'X-Weather-Key-Id': key_id,
        'X-Weather-Timestamp': ts,
        'X-Weather-Nonce': nonce_hex,
        'X-Weather-Signature': signature,
    }
    return body, headers
