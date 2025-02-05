import time

import numpy as np

from dwave.optimization import Model
from dwave.optimization.mathematical import add, multiply


def heavy(n: int, num_states: int = 100, seed: int = 42) -> Model:
    """Create a "heavy" model with n integer variables in a single symbol."""
    model = Model()

    x = model.integer(n, lower_bound=-1000, upper_bound=1000)

    model.minimize(-x.sum())
    model.add_constraint(x.prod() <= model.constant(1000))

    states = np.random.default_rng(seed).integers(-1000, 1000, size=(num_states, n))

    model.states.resize(num_states)
    for i, state in enumerate(states):
        x.set_state(i, state)

    return model


def tall(n: int, num_states: int = 100, seed: int = 42) -> Model:
    """Create a "tall" model with one scalar integer used in n operations."""
    model = Model()

    x = model.integer(lower_bound=-1000, upper_bound=1000)
    c = model.constant(0)

    out = x
    for _ in range(n):
        out = out + c

    model.minimize(-out)
    model.add_constraint(x <= c)

    states = np.random.default_rng(seed).integers(-1000, 1000, size=(num_states,))

    model.states.resize(num_states)
    for i, state in enumerate(states):
        x.set_state(i, state)

    return model


def wide(n: int, num_states: int = 100, seed: int = 42) -> Model:
    """Create a "wide" model with n integer variables each as a scalar."""
    model = Model()

    xs = [model.integer(lower_bound=-1000, upper_bound=1000) for _ in range(n)]

    model.minimize(-add(*xs))
    model.add_constraint(multiply(*xs) <= model.constant(1000))

    states = np.random.default_rng(seed).integers(-1000, 1000, size=(num_states, n))

    model.states.resize(num_states)
    for i, state in enumerate(states):
        for j, val in enumerate(state):
            xs[j].set_state(i, val)

    return model


# for each model, measure how long it takes to serialize the model and the states,
# and how large the serialzied file is
for name, n, func in [("heavy", 100_000, heavy), ("tall", 100_000, tall), ("wide", 500, wide)]:
    print(f"**** generator: {name}({n})")
    model = func(n)

    t = time.perf_counter()
    with model.to_file():
        pass
    print("model serization time:", time.perf_counter() - t)

    t = time.perf_counter()
    with model.states.to_file():
        pass
    print("state serization time:", time.perf_counter() - t)

    with model.to_file() as f:
        print("model serization size:", len(f.read()))

    with model.states.to_file() as f:
        print("state serization size:", len(f.read()))
