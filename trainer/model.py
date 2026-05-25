"""Float HalfKP model matching the C inference arch (headers/nnue.h).

cp = forward(...) * OUTPUT_SCALE.  See serialize.py for the exact mapping.
"""

import torch
import torch.nn as nn

from citt_bridge import INPUT, L1, FT_OUT, FC_OUT, QB, OUTPUT_SCALE

# Back-layer float weights must fit int8 after *QB: |w| <= 127/QB.
WEIGHT_CLAMP = 127.0 / QB

class HalfKP(nn.Module):
    def __init__(self):
        super().__init__()
        self.ft = nn.EmbeddingBag(INPUT, L1, mode="sum")
        self.ft_bias = nn.Parameter(torch.zeros(L1))
        self.fc0 = nn.Linear(FT_OUT, FC_OUT)
        self.fc1 = nn.Linear(FC_OUT, FC_OUT)
        self.fc2 = nn.Linear(FC_OUT, 1)

        nn.init.normal_(self.ft.weight, std=0.01)

    def _perspective(self, idx, offsets):
        return torch.clamp(self.ft(idx, offsets) + self.ft_bias, 0.0, 1.0)

    def forward(self, white_idx, white_off, black_idx, black_off, stm):
        acc_w = self._perspective(white_idx, white_off)
        acc_b = self._perspective(black_idx, black_off)

        # Order the concat by side to move: stm's perspective goes first.
        stm_f = stm.view(-1, 1).float()
        first = acc_w * (1.0 - stm_f) + acc_b * stm_f
        second = acc_b * (1.0 - stm_f) + acc_w * stm_f
        x = torch.cat([first, second], dim=1)

        x = torch.clamp(self.fc0(x), 0.0, 1.0)
        x = torch.clamp(self.fc1(x), 0.0, 1.0)
        x = self.fc2(x)
        return x.squeeze(1)

    def cp(self, *args):
        """Forward, scaled to centipawns (the unit the C evaluator returns)."""
        return self.forward(*args) * OUTPUT_SCALE

    @torch.no_grad()
    def clamp_weights(self):
        """Keep back-layer weights inside the int8-representable range so
        post-training quantization is faithful (range-aware training)."""
        for layer in (self.fc0, self.fc1, self.fc2):
            layer.weight.clamp_(-WEIGHT_CLAMP, WEIGHT_CLAMP)
