# CITT NNUE trainer

PyTorch training pipeline for CITT's HalfKP NNUE. PyTorch is used as the
training engine and gradient oracle; the long-term plan is to swap its pieces
for the project's own TinyBLAS/GEMM kernels incrementally (see the plan file).

The C engine is the source of truth: feature indexing and the quantized
evaluator are reused via `libcittnnue.so` (ctypes), never reimplemented here.

## Layout

- `citt_bridge.py` — ctypes wrapper for `libcittnnue.so`; arch constants,
  quant scales, datagen record format, dataset reader.
- `model.py` — float HalfKP model matching `headers/nnue.h`.
- `data.py` — dataset + collate (features come from the C bridge).
- `train.py` — training loop (eval+WDL blend loss, Adam, weight clamping).
- `quant.py` — quantization math + `.net` writer + integer reference forward.
- `serialize.py` — checkpoint → `.net`.
- `parity_check.py` — gate: C eval vs integer reference (exact) and vs float model.

## Workflow

```sh
# 0. build the engine + bridge (from repo root)
make libcittnnue
make datagen

# 1. generate self-play data (classic eval bootstrap)
./build/bin/citt-datagen --games 20000 --depth 8 --out data.bin

# 2. set up python (GPU box)
python -m venv .venv && . .venv/bin/activate
pip install -r trainer/requirements.txt

# 3. train
python trainer/train.py --data data.bin --epochs 20 --batch-size 16384 --out ckpt.pt

# 4. serialize to CITT's net format
python trainer/serialize.py --ckpt ckpt.pt --out net.bin

# 5. parity gate (must print PARITY OK)
python trainer/parity_check.py --ckpt ckpt.pt --data data.bin

# 6. play with it
CITT_EVAL_FILE=net.bin ./build/bin/citt-bench
#   or: SPRT vs the classic baseline via tools/sprt.sh
```

`data.bin` and `*.net` / `*.pt` are gitignored (large/binary).
