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

from libcpp cimport bool

from dwave.optimization.libcpp.graph cimport Graph as cppGraph


cdef class _Graph:
    cpdef bool is_locked(self) noexcept
    cpdef Py_ssize_t num_constraints(self) noexcept
    cpdef Py_ssize_t num_edges(self) noexcept
    cpdef Py_ssize_t num_decisions(self) noexcept
    cpdef Py_ssize_t num_nodes(self) noexcept

    # Make the _Graph class weak-referenceable. This is used by States
    cdef object __weakref__

    cdef cppGraph _graph

    # The number of times "lock()" has been called.
    cdef readonly Py_ssize_t _lock_count
