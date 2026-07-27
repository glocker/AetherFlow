# Compatibility and golden vectors

`compat/` stores protocol compatibility notes and committed golden vectors for the AetherFlow CAN Protocol.

Golden vectors are byte-level fixtures: each vector records a semantic packet and the exact CAN frames expected on the wire. They protect the protocol from accidental changes while the project evolves.

## Files

```text
compat/
  README.md
  vectors/
    aetherflow_can_vectors.json
  python/
    check_vectors.py
```

Golden-vector test coverage lives in:

```text
tests/python/test_vectors.py
```

## Current vector scope

The golden vectors cover:

- CAN ID conventions;
- `service/subtype/payload` packet format;
- single-frame fragmentation;
- multi-frame fragmentation;
- reassembly expectations;
- the current EPS housekeeping payload layout.

## Usage

From the repository root:

```sh
python compat/python/check_vectors.py compat/vectors/aetherflow_can_vectors.json
python -m pytest tests/python/test_vectors.py
```

Optional LibreCube/CSP probe:

```sh
python compat/python/check_vectors.py --backend librecube
```

The LibreCube probe is non-fatal. It currently only checks whether likely Python packages are importable.

## LibreCube/CSP TODO



- [ ] Does the package install cleanly on current Ubuntu/Python?
- [ ] What is the actual import name and public API?
- [ ] Does it support CAN fragmentation/reassembly?
- [ ] What CAN ID conventions does it use?
- [ ] Does it match AetherFlow's `service/subtype/payload` packet shape?
- [ ] Can it coexist with project-specific EPS payload schemas?
- [ ] Would it reduce code?
