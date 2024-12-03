# Copyright 2024 D-Wave
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.


cdef class _Graph:
    cpdef bool is_locked(self) noexcept:
        """Lock status of the model.

        No new symbols can be added to a locked model.

        See also:
            :meth:`.lock`, :meth:`.unlock`
        """
        return self._graph.topologically_sorted()

    def lock(self):
        self._graph.topological_sort()  # does nothing if already sorted, so safe to call always
        self._lock_count += 1

    cpdef Py_ssize_t num_constraints(self) noexcept:
        """Number of constraints in the model.

        Examples:
            This example checks the number of constraints in the model after
            adding a couple of constraints.

            >>> from dwave.optimization.model import Model
            >>> model = Model()
            >>> i = model.integer()
            >>> c = model.constant([5, -14])
            >>> model.add_constraint(i <= c[0]) # doctest: +ELLIPSIS
            <dwave.optimization.symbols.LessEqual at ...>
            >>> model.add_constraint(c[1] <= i) # doctest: +ELLIPSIS
            <dwave.optimization.symbols.LessEqual at ...>
            >>> model.num_constraints()
            2
        """
        return self._graph.num_constraints()

    cpdef Py_ssize_t num_decisions(self) noexcept:
        """Number of independent decision nodes in the model.

        An array-of-integers symbol, for example, counts as a single
        decision node.

        Examples:
            This example checks the number of decisions in a model after
            adding a single (size 20) decision symbol.

            >>> from dwave.optimization.model import Model
            >>> model = Model()
            >>> c = model.constant([1, 5, 8.4])
            >>> i = model.integer(20, upper_bound=100)
            >>> model.num_decisions()
            1
        """
        return self._graph.num_decisions()

    cpdef Py_ssize_t num_edges(self) noexcept:
        """Number of edges in the directed acyclic graph for the model.

        Examples:
            This example minimizes the sum of a single constant symbol and
            a single decision symbol, then checks the number of edges in
            the model.

            >>> from dwave.optimization.model import Model
            >>> model = Model()
            >>> c = model.constant(5)
            >>> i = model.integer()
            >>> model.minimize(c + i)
            >>> model.num_edges()
            2
        """
        cdef Py_ssize_t num_edges = 0
        for i in range(self._graph.num_nodes()):
            num_edges += self._graph.nodes()[i].get().successors().size()
        return num_edges

    cpdef Py_ssize_t num_nodes(self) noexcept:
        """Number of nodes in the directed acyclic graph for the model.

        See also:
            :meth:`.num_symbols`

        Examples:
            This example add a single (size 20) decision symbol and
            a single (size 3) constant symbol checks the number of
            nodes in the model.

            >>> from dwave.optimization.model import Model
            >>> model = Model()
            >>> c = model.constant([1, 5, 8.4])
            >>> i = model.integer(20, upper_bound=100)
            >>> model.num_nodes()
            2
        """
        return self._graph.num_nodes()

    def _remove_unused_nodes(self):
        if self.is_locked():
            raise ValueError("cannot remove symbols from a locked model")
        return self._graph.remove_unused_nodes()

    def unlock(self):
        """Release a lock, decrementing the lock count.

        Symbols can be added to unlocked models only.

        See also:
            :meth:`.is_locked`, :meth:`.lock`
        """
        if self._lock_count <= 0:
            # already unlocked, nothing to do
            return

        self._lock_count -= 1

        if self._lock_count > 0:
            # if there are locks still to be released, then exit
            return

        self._graph.reset_topological_sort()
