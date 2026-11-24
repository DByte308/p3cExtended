#!/usr/bin/env python3
"""columns.py <pickle>: per-stream, per-column statistics of recorded TTP notifications (see ttp.py)."""
import pickle, sys, collections; sys.path.insert(0, __file__.rsplit("/", 1)[0]); import ttp
def cols(p):
    items = list(ttp.tlv_walk(p, 2)); out = {}; i = 0
    while i < len(items):
        off, t, v = items[i]
        if t == 5 and v == 134073 and i + 1 < len(items) and items[i + 1][1] == 2:
            cid = items[i + 1][2]; i += 2; vals = []
            while i < len(items) and not (items[i][1] == 5 and items[i][2] == 134073): vals.append((items[i][1], items[i][2])); i += 1
            out[cid] = vals
        else: i += 1
    return out
if __name__ == "__main__":
    got = pickle.load(open(sys.argv[1], "rb"))
    for sid, lst in got.items():
        print("stream", sid, len(lst), "msgs")
        stats = collections.defaultdict(list)
        for t, p in lst:
            for cid, vals in cols(p).items(): stats[cid].append(tuple(v for _, v in vals))
        print("  col  types  distinct   min..max of first value / sample (mid)")
        for cid in sorted(stats):
            vs = stats[cid]; ty = ",".join(str(t) for t, _ in cols(lst[0][1])[cid])
            f = lambda x: tuple(round(a, 3) if isinstance(a, float) else a for a in x)
            nums = [v[0] for v in vs if isinstance(v[0], (int, float))]
            rng = f"{round(min(nums),3)}..{round(max(nums),3)}" if nums else ""
            print(f"  {cid:3d}  {ty:9s} {len(set(vs)):6d}   {rng:22s} {f(vs[len(vs)//2])}")
