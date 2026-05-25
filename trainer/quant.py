"""Quantization arithmetic and .net writer (no torch dependency).

`write_net` produces the exact byte layout src/nnue.c `nnue_load` parses.
`propagate_int` is a numpy reimplementation of the integer forward pass in
nnue_propagate; it is the reference the parity check compares the C path
against, and lets the serializer be validated without a trained model.
"""

import numpy as np

from citt_bridge import (INPUT, L1, FT_OUT, FC_OUT, QA, QB, FT_SCALE,
                         BIAS_SCALE, OUTPUT_SCALE)


def write_net(path, ft_w, ft_b, fc0_w, fc0_b, fc1_w, fc1_b, fc2_w, fc2_b):
    """All arrays are integer numpy arrays in their natural 2D/1D shapes;
    written little-endian in the order nnue_load expects."""
    with open(path, "wb") as f:
        f.write(b"CITTNNUE")
        np.array([1, INPUT, L1, FT_OUT, FC_OUT], dtype="<u4").tofile(f)
        ft_w.astype("<i2").ravel().tofile(f)
        ft_b.astype("<i2").ravel().tofile(f)
        fc0_w.astype("<i1").ravel().tofile(f)
        fc0_b.astype("<i4").ravel().tofile(f)
        fc1_w.astype("<i1").ravel().tofile(f)
        fc1_b.astype("<i4").ravel().tofile(f)
        fc2_w.astype("<i1").ravel().tofile(f)
        np.array([fc2_b], dtype="<i4").tofile(f)


def _crelu(x):
    return np.clip(x, 0, QA)


def _crelu_scaled(x):
    # C does integer division `s / QB` (truncation toward zero) then clips.
    return np.clip(np.fix(x / QB), 0, QA)


def propagate_int(net, white_idx, black_idx, stm):
    """net: dict of int arrays (ft_w [INPUT,L1], ft_b [L1], fc0_w [FC_OUT,FT_OUT],
    fc0_b [FC_OUT], fc1_w [FC_OUT,FC_OUT], fc1_b [FC_OUT], fc2_w [FC_OUT],
    fc2_b scalar). white_idx/black_idx: active feature indices. Returns cp
    from stm's perspective, mirroring src/nnue.c nnue_propagate exactly."""
    acc_w = net["ft_b"].astype(np.int64).copy()
    acc_b = net["ft_b"].astype(np.int64).copy()
    if len(white_idx):
        acc_w += net["ft_w"][white_idx].sum(axis=0)
    if len(black_idx):
        acc_b += net["ft_w"][black_idx].sum(axis=0)

    ft_w_act = _crelu(acc_w)
    ft_b_act = _crelu(acc_b)
    x = (np.concatenate([ft_w_act, ft_b_act]) if stm == 0
         else np.concatenate([ft_b_act, ft_w_act])).astype(np.int64)

    h0 = _crelu_scaled(net["fc0_b"].astype(np.int64) + net["fc0_w"].astype(np.int64) @ x)
    h1 = _crelu_scaled(net["fc1_b"].astype(np.int64) + net["fc1_w"].astype(np.int64) @ h0)
    out = int(net["fc2_b"]) + int(net["fc2_w"].astype(np.int64) @ h1)
    return int(np.fix(out * OUTPUT_SCALE / (QA * QB)))


def quantize_state(sd):
    """Torch state_dict -> dict of int numpy arrays in the shapes above.
    Mirrors the scales in src/nnue.c (FT *127 int16; back-layer w *64 int8,
    b *(QA*QB) int32)."""
    def q(t):
        return t.detach().cpu().numpy()

    def round_clip(a, scale, lo, hi):
        return np.clip(np.rint(a * scale), lo, hi)

    return {
        "ft_w":  round_clip(q(sd["ft.weight"]), FT_SCALE, -32767, 32767).astype(np.int32),
        "ft_b":  round_clip(q(sd["ft_bias"]),   FT_SCALE, -32767, 32767).astype(np.int32),
        "fc0_w": round_clip(q(sd["fc0.weight"]), QB, -127, 127).astype(np.int32),
        "fc0_b": np.rint(q(sd["fc0.bias"]) * BIAS_SCALE).astype(np.int64),
        "fc1_w": round_clip(q(sd["fc1.weight"]), QB, -127, 127).astype(np.int32),
        "fc1_b": np.rint(q(sd["fc1.bias"]) * BIAS_SCALE).astype(np.int64),
        "fc2_w": round_clip(q(sd["fc2.weight"]).reshape(FC_OUT), QB, -127, 127).astype(np.int32),
        "fc2_b": int(np.rint(float(q(sd["fc2.bias"])[0]) * BIAS_SCALE)),
    }


def save_net(net, path):
    write_net(path, net["ft_w"], net["ft_b"], net["fc0_w"], net["fc0_b"],
              net["fc1_w"], net["fc1_b"], net["fc2_w"], net["fc2_b"])
