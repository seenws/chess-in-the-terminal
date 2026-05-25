"""ctypes bridge to libcittnnue.so.

Single source of truth for HalfKP feature indexing and the quantized
evaluator: both live in the C engine and are reused here so Python never
reimplements them. Also holds the architecture constants and quantization
scales that the trainer and serializer must match (mirrors headers/nnue.h
and the arithmetic in src/nnue.c).
"""

import ctypes
import os

import numpy as np

# --- architecture (must match headers/nnue.h) ---
INPUT = 40960
L1 = 256
FT_OUT = 512          # 2 * L1
FC_OUT = 32
MAX_FEAT = 32         # >= 30 active features per perspective

# --- quantization scales (must match src/nnue.c nnue_propagate) ---
QA = 127              # activation scale: float [0,1] -> int [0,127]
QB = 64               # weight scale for back layers: 2 ** WEIGHT_SCALE_BITS
FT_SCALE = 127        # feature-transformer weight/bias scale
BIAS_SCALE = QA * QB  # 8128: back-layer bias scale
OUTPUT_SCALE = 16     # raw float output -> centipawns is (* OUTPUT_SCALE)

# --- datagen record format (must match src/datagen.c) ---
DATA_MAGIC = b"CITTDATA"
DATA_HEADER = 16      # magic(8) + u32 version + u32 record_size
RECORD_SIZE = 36      # board(32) + stm(1) + score(i16) + result(1)

_DEFAULT_LIB = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "build", "lib", "libcittnnue.so",
)


class CittBridge:
    def __init__(self, lib_path=_DEFAULT_LIB):
        self.lib = ctypes.CDLL(lib_path)

        self.lib.citt_extract_batch.restype = None
        self.lib.citt_extract_batch.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_int, ctypes.c_int,
            ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_int32),
            ctypes.POINTER(ctypes.c_int32),
        ]
        self.lib.nnue_load.restype = ctypes.c_int
        self.lib.nnue_load.argtypes = [ctypes.c_char_p]
        self.lib.nnue_unload.restype = None
        self.lib.nnue_available.restype = ctypes.c_bool
        self.lib.citt_eval_board.restype = ctypes.c_int
        self.lib.citt_eval_board.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]

    def extract(self, boards):
        """boards: uint8 [N, 64] mailbox. Returns (white_idx, black_idx, counts).

        white_idx / black_idx are int32 [N, MAX_FEAT]; only the first
        counts[i] entries of each row are valid.
        """
        boards = np.ascontiguousarray(boards, dtype=np.uint8)
        n = boards.shape[0]
        white = np.zeros((n, MAX_FEAT), dtype=np.int32)
        black = np.zeros((n, MAX_FEAT), dtype=np.int32)
        counts = np.zeros(n, dtype=np.int32)
        self.lib.citt_extract_batch(
            boards.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8)),
            n, MAX_FEAT,
            white.ctypes.data_as(ctypes.POINTER(ctypes.c_int32)),
            black.ctypes.data_as(ctypes.POINTER(ctypes.c_int32)),
            counts.ctypes.data_as(ctypes.POINTER(ctypes.c_int32)),
        )
        return white, black, counts

    def load_net(self, path):
        return self.lib.nnue_load(path.encode())

    def unload_net(self):
        self.lib.nnue_unload()

    def available(self):
        return bool(self.lib.nnue_available())

    def eval_board(self, board, stm):
        board = np.ascontiguousarray(board, dtype=np.uint8)
        return self.lib.citt_eval_board(
            board.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8)), int(stm))


def unpack_boards(packed):
    """Expand nibble-packed boards [N, 32] (uint8) to mailboxes [N, 64]."""
    packed = np.ascontiguousarray(packed, dtype=np.uint8)
    n = packed.shape[0]
    out = np.zeros((n, 64), dtype=np.uint8)
    out[:, 0::2] = packed & 0x0F
    out[:, 1::2] = packed >> 4
    return out


def load_dataset(path):
    """Reads a datagen .bin. Returns (boards[N,64] uint8, stm[N], score[N] i16,
    result[N] in {0,1,2} white POV)."""
    with open(path, "rb") as f:
        header = f.read(DATA_HEADER)
        magic = header[:8]
        version, rsize = np.frombuffer(header[8:16], dtype="<u4")
        if magic != DATA_MAGIC or rsize != RECORD_SIZE:
            raise ValueError(f"bad data file: magic={magic} rsize={rsize}")
        raw = np.frombuffer(f.read(), dtype=np.uint8)

    n = raw.size // RECORD_SIZE
    raw = raw[: n * RECORD_SIZE].reshape(n, RECORD_SIZE)
    boards = unpack_boards(raw[:, :32])
    stm = raw[:, 32].astype(np.int64)
    score = raw[:, 33:35].copy().view("<i2").reshape(n).astype(np.int64)
    result = raw[:, 35].astype(np.int64)
    return boards, stm, score, result
