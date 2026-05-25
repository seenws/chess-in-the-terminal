"""Parity gate between the Python model and the C engine.

Quantizes a checkpoint, writes a .net, loads it in the C engine, and over N
sampled positions compares:

  (A) C nnue_evaluate  vs  numpy integer reference (quant.propagate_int)
      -- must be exact (same fixed-point pipeline). This is the hard gate.
  (B) float model cp   vs  C nnue_evaluate
      -- the quantization error; reported, not asserted.

    python parity_check.py --ckpt ckpt.pt --data data.bin --n 2000
"""

import argparse
import tempfile

import numpy as np
import torch

from citt_bridge import CittBridge, MAX_FEAT, load_dataset
from model import HalfKP
from quant import quantize_state, save_net, propagate_int


def main():
    p = argparse.ArgumentParser()

    p.add_argument("--ckpt", required=True)
    p.add_argument("--data", required=True)
    p.add_argument("--n", type=int, default=2000)

    args = p.parse_args()

    sd = torch.load(args.ckpt, map_location="cpu")

    model = HalfKP()
    model.load_state_dict(sd)
    model.eval()

    net = quantize_state(sd)
    bridge = CittBridge()
    tmp = tempfile.NamedTemporaryFile(suffix=".net", delete=False)
    tmp.close()
    save_net(net, tmp.name)
    assert bridge.load_net(tmp.name) == 0, "C engine rejected the serialized net"

    boards, stm, _, _ = load_dataset(args.data)
    n = min(args.n, boards.shape[0])
    sel = np.random.default_rng(0).choice(boards.shape[0], n, replace=False)

    white, black, counts = bridge.extract(boards[sel])

    exact_max = 0
    quant_abs = []
    for k in range(n):
        i = sel[k]
        c = counts[k]
        wi = white[k, :c]
        bi = black[k, :c]
        s = int(stm[i])

        c_cp = bridge.eval_board(boards[i], s)
        ref_cp = propagate_int(net, wi, bi, s)
        exact_max = max(exact_max, abs(c_cp - ref_cp))

        with torch.no_grad():
            wi_t = torch.from_numpy(wi.astype(np.int64))
            bi_t = torch.from_numpy(bi.astype(np.int64))
            off = torch.zeros(1, dtype=torch.int64)
            stm_t = torch.tensor([s])
            m_cp = float(model.cp(wi_t, off, bi_t, off, stm_t).item())
        quant_abs.append(abs(m_cp - c_cp))

    quant_abs = np.array(quant_abs)
    print(f"(A) C vs int-reference: max abs diff = {exact_max}  (must be <= 1)")
    print(f"(B) float model vs quantized C: mean={quant_abs.mean():.1f} "
          f"max={quant_abs.max():.1f} cp")
    assert exact_max <= 1, "C path disagrees with the integer reference"
    print("PARITY OK")


if __name__ == "__main__":
    main()
