"""Dataset + collate that turn datagen records into EmbeddingBag inputs.

Feature indices come from the C bridge (extract), so indexing can never drift
from the engine. The collate flattens each perspective's variable-length
active features into the (input, offsets) form EmbeddingBag wants.
"""

import numpy as np
import torch
from torch.utils.data import Dataset, DataLoader

from citt_bridge import CittBridge, MAX_FEAT, load_dataset


class CittDataset(Dataset):
    def __init__(self, path):
        self.boards, self.stm, self.score, self.result = load_dataset(path)

    def __len__(self):
        return self.boards.shape[0]

    def __getitem__(self, i):
        return self.boards[i], self.stm[i], self.score[i], self.result[i]


def _flatten(idx_rows, counts):
    """idx_rows [B, MAX_FEAT], counts [B] -> (flat_idx, offsets) for EmbeddingBag."""
    keep = np.arange(MAX_FEAT)[None, :] < counts[:, None]
    flat = idx_rows[keep]
    offsets = np.zeros(len(counts), dtype=np.int64)
    offsets[1:] = np.cumsum(counts)[:-1]
    return torch.from_numpy(flat.astype(np.int64)), torch.from_numpy(offsets)


class Collate:
    def __init__(self, bridge):
        self.bridge = bridge

    def __call__(self, batch):
        boards = np.stack([b[0] for b in batch]).astype(np.uint8)
        stm = np.array([b[1] for b in batch], dtype=np.int64)
        score = np.array([b[2] for b in batch], dtype=np.float32)
        result = np.array([b[3] for b in batch], dtype=np.float32)

        white, black, counts = self.bridge.extract(boards)
        wi, wo = _flatten(white, counts)
        bi, bo = _flatten(black, counts)

        return {
            "white_idx": wi, "white_off": wo,
            "black_idx": bi, "black_off": bo,
            "stm": torch.from_numpy(stm),
            "score": torch.from_numpy(score),
            "result": torch.from_numpy(result),  # white POV in {0,1,2}
        }


def make_loader(path, batch_size, shuffle=True, num_workers=0, bridge=None):
    bridge = bridge or CittBridge()
    ds = CittDataset(path)
    return DataLoader(ds, batch_size=batch_size, shuffle=shuffle,
                      num_workers=num_workers, collate_fn=Collate(bridge),
                      drop_last=True)
