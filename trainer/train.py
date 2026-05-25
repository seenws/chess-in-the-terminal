"""Train the HalfKP net on datagen self-play data.

Target blends the search score and the game result:
    target = lambda * sigmoid(score / S) + (1 - lambda) * result_stm
where result_stm is the game outcome from the side-to-move's view. The model
predicts a centipawn score; loss is MSE in sigmoid (win-probability) space.

Run on the GPU box, e.g.:
    python train.py --data data.bin --epochs 20 --batch-size 16384 --out ckpt.pt
"""

import argparse

import torch
import torch.nn.functional as F

from data import make_loader
from model import HalfKP


def result_stm(result, stm):
    """Game result (white POV {0,1,2}) oriented to the side to move, in [0,1]."""
    wpov = result / 2.0
    return torch.where(stm == 0, wpov, 1.0 - wpov)


def train(args):
    device = args.device or ("cuda" if torch.cuda.is_available() else "cpu")
    print(f"device: {device}")

    loader = make_loader(args.data, args.batch_size, num_workers=args.num_workers)
    model = HalfKP().to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    sched = torch.optim.lr_scheduler.StepLR(opt, step_size=args.lr_step, gamma=args.lr_gamma)

    for epoch in range(args.epochs):
        model.train()
        running, batches = 0.0, 0

        for b in loader:
            wi = b["white_idx"].to(device); wo = b["white_off"].to(device)
            bi = b["black_idx"].to(device); bo = b["black_off"].to(device)
            stm = b["stm"].to(device)
            score = b["score"].to(device)
            result = b["result"].to(device)

            target = (args.lam * torch.sigmoid(score / args.scale)
                      + (1.0 - args.lam) * result_stm(result, stm))

            pred = torch.sigmoid(model.cp(wi, wo, bi, bo, stm) / args.scale)
            loss = F.mse_loss(pred, target)

            opt.zero_grad()
            loss.backward()
            opt.step()
            model.clamp_weights()

            running += loss.item()
            batches += 1

        sched.step()
        print(f"epoch {epoch + 1}/{args.epochs}  loss {running / max(batches, 1):.6f}")
        torch.save(model.state_dict(), args.out)

    print(f"saved checkpoint to {args.out}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--data", required=True, help="datagen .bin file")
    p.add_argument("--out", default="ckpt.pt", help="checkpoint output path")
    p.add_argument("--epochs", type=int, default=20)
    p.add_argument("--batch-size", type=int, default=16384)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--lr-step", type=int, default=8)
    p.add_argument("--lr-gamma", type=float, default=0.3)
    p.add_argument("--lam", type=float, default=0.7, help="eval vs result weight")
    p.add_argument("--scale", type=float, default=400.0, help="cp per sigmoid unit")
    p.add_argument("--num-workers", type=int, default=0)
    p.add_argument("--device", default=None, help="cuda / cpu (default: auto)")
    train(p.parse_args())


if __name__ == "__main__":
    main()
