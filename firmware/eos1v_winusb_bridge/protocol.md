# Open1V bridge protocol v1

All multibyte integers are little-endian.

## Frame

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Magic `4F 31` (`O1`) |
| 2 | 1 | Protocol version, currently `01` |
| 3 | 1 | Message type |
| 4 | 2 | Sequence number |
| 6 | 2 | Payload length, maximum 512 |
| 8 | N | Payload |
| 8+N | 2 | CRC-16/CCITT-FALSE over bytes 0 through 7+N |

CRC parameters: polynomial `0x1021`, initial value `0xFFFF`, no reflection,
no final XOR.

Frames are self-synchronizing by magic and length. A receiver discards bytes
until magic is found. Invalid version, excessive length or bad CRC produces an
error response when a usable request header was received.

Response message type is the request type with bit 7 set. Sequence numbers are
copied unchanged.

## Common response prefix

Every response payload begins with one status byte:

| Value | Meaning |
| ---: | --- |
| `00` | OK |
| `01` | Unknown message type |
| `02` | Invalid request payload |
| `03` | CRC error |
| `04` | Camera reply timeout or partial reply |
| `05` | Internal limit exceeded |

## `01` PING

Request payload is empty.

Successful response payload:

```text
00 01 52 41 34
```

This is status OK, protocol version 1 and ASCII `RA4`.

## `02` GET_STATUS

Request payload is empty.

Successful response payload:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | Status (`00`) |
| 1 | 1 | DATA-A sense level (`00` low, `01` high) |
| 2 | 1 | High-assist active (`00`/`01`) |
| 3 | 4 | Completed exchange count |
| 7 | 4 | Framing/CRC error count |

## `10` EXCHANGE

Request payload:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Expected reply byte count, 0..512 |
| 2 | 2 | Timeout for first byte in milliseconds, 1..5000 |
| 4 | 2 | Timeout between later bytes in milliseconds, 1..5000 |
| 6 | 2 | Transmit byte count, 0..64 |
| 8 | M | Bytes to transmit to the camera |

The RA4 sends transmit bytes in order with the proven adaptive line driver,
then reads up to the exact expected count. It never retries or interprets
camera protocol bytes. This prevents an MCU retry from accidentally repeating
a state-changing camera command.

Successful response payload:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | Status (`00`) |
| 1 | 2 | Received count |
| 3 | N | Camera reply bytes |

If fewer bytes arrive before a timeout, status is `04`; the partial bytes and
their count are still returned.

## `11` RELEASE

Request payload is empty. The RA4 immediately disables both line drivers,
clears the high-assist state and drains the camera UART receive buffer.

Successful response payload is one byte: `00`.
