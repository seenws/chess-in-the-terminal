"""Convert a trained checkpoint into a CITT .net file.

    python serialize.py --ckpt ckpt.pt --out net.bin
"""

import argparse

import torch

from quant import quantize_state, save_net


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--ckpt", required=True)
    p.add_argument("--out", default="net.bin")
    args = p.parse_args()

    sd = torch.load(args.ckpt, map_location="cpu")
    net = quantize_state(sd)
    save_net(net, args.out)
    print(f"wrote quantized net to {args.out}")


if __name__ == "__main__":
    main()
