// Copyright 2025 D-Wave
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "dwave-optimization/array.hpp"
#include "dwave-optimization/common.hpp"
#include "dwave-optimization/graph.hpp"
#include "dwave-optimization/interval.hpp"
#include "dwave-optimization/operator.hpp"
#include "dwave-optimization/state.hpp"

namespace dwave::optimization {

template <class BinaryOp>
class BinaryOpNode : public ArrayOutputMixin<EqualityMixin<ArrayNode, BinaryOpNode<BinaryOp>>> {
 public:
    BinaryOpNode(BinaryOp&& op, ArrayNode* lhs_ptr, ArrayNode* rhs_ptr) :
        ArrayOutputMixin<EqualityMixin<ArrayNode, BinaryOpNode<BinaryOp>>>(
            broadcast_shapes(*lhs_ptr, *rhs_ptr)
        ),
        op(std::move(op)),
        operands_{lhs_ptr, rhs_ptr},
        // operand_strides_{
        //     broadcast_strides_to(lhs_ptr->shape(), lhs_ptr->strides(), shape()),
        //     broadcast_strides_to(rhs_ptr->shape(), rhs_ptr->strides(), shape())
        // },
        domain_(calculate_domain_(op, lhs_ptr, rhs_ptr)),
        integral_(calculate_integral_(op, lhs_ptr, rhs_ptr)),
        sizeinfo_(calculate_sizeinfo_(shape(), lhs_ptr, rhs_ptr)) {
        this->add_predecessor_(lhs_ptr);
        this->add_predecessor_(rhs_ptr);
    }

    BinaryOpNode(ArrayNode* lhs_ptr, ArrayNode* rhs_ptr) : BinaryOpNode({}, lhs_ptr, rhs_ptr) {}

    const double* buff(const State& state) const override {
        return this->template data_ptr_<BinaryOpState_>(state)->buffer.data();
    }

    void commit(State& state) const override {
        assert(false and "not yet implememented");
        // auto* state_ptr = this->template data_ptr_<BinaryOpState_>(state);

        // // Don't need to do anything to the buffer

        // // Mark our various seens as false
        // // In some cases it might actually be faster to just overwrite the entire
        // // `seen` vector rather than doing this sparsely. But for now let's go
        // // with the simple/sparse thing.
        // for (const auto& [index, _, __] : state_ptr->diff) state_ptr->seen[index] = false;
        // assert(std::ranges::none_of(state_ptr->seen, [](auto x) { return x; }));

        // // clear the diff
        // state_ptr->diff.clear();

        // // don't need to change the shape

        // // update the previous_num_rows to match the current length
        // if (this->dynamic()) state_ptr->previous_num_rows = state_ptr->shape[0];

        // // row_size is const
    }

    std::span<const Update> diff(const State& state) const override {
        return this->template data_ptr_<BinaryOpState_>(state)->diff;
    }

    /// Two BinaryOpNodes are equal if they have the same operation and the
    /// same predecessors. If the BinaryOp is commutative, then permutations
    /// of predecessors are allowed.
    bool equal_to(const BinaryOpNode& rhs) const override {
        if constexpr (BinaryOp::commutative) {
            return std::ranges::is_permutation(operands_, rhs.operands_);
        } else {
            return std::ranges::equal(operands_, rhs.operands_);
        }
    }

    void initialize_state(State& state) const override {
        auto broadcaster = broadcast_(operands_[0], operands_[1]);

        // See note on broadcast_::statefull_shape_
        broadcaster.update_shape(state);

        // Read our statefull shape off of our predecessors for later caching
        std::vector<ssize_t> shape = std::ranges::to<std::vector>(broadcaster.shape(state));

        // Get views of our predecessors
        const auto [lhs_view, rhs_view] = broadcaster.views(state);
        assert(std::size(lhs_view) == std::size(rhs_view));

        // And from those, it's easy to get our buffer
        std::vector<double> buffer = std::ranges::to<std::vector<double>>(
            std::views::zip(lhs_view, rhs_view) |
            std::views::transform([&op = this->op](const auto& xy) -> double {
                return op(std::get<0>(xy), std::get<1>(xy));
            })
        );

        // /// TODO REMOVE *******
        // std::cout << "&&&&&&&&&&&&&\n";
        // for (auto v : operand_strides[0]) std::cout << v << " ";
        // std::cout << " <- lhs_strides ****\n";
        // for (auto v : operand_strides[1]) std::cout << v << " ";
        // std::cout << " <- rhs_strides ****\n";
        // for (auto v : operand_indices[0]) std::cout << v << " ";
        // std::cout << " <- lhs indices ****\n";
        // for (auto v : operand_indices[1]) std::cout << v << " ";
        // std::cout << " <- rhs indices ****\n";
        // for (auto v : lhs_view) std::cout << v << " ";
        // std::cout << " <- lhs_view ****\n";
        // for (auto v : rhs_view) std::cout << v << " ";
        // std::cout << " <- rhs_view ****\n";
        // for (auto v : shape) std::cout << v << " ";
        // std::cout << " <- shape ****\n";
        // for (auto v : buffer) std::cout << v << " ";
        // std::cout << " <- buffer ****\n";
        // /// *******************

        // Store everything on the state
        this->template emplace_data_ptr_<BinaryOpState_>(
            state, std::move(broadcaster), std::move(shape), std::move(buffer)
        );
    }

    /// @copydoc Array::integral()
    bool integral() const override { return integral_; }

    /// @copydoc Array::max()
    double max() const override { return domain_.supremum; }

    /// @copydoc Array::min()
    double min() const override { return domain_.infimum; }

    // The predecessors of the operation, as ArrayNode*.
    std::span<ArrayNode* const> operands() {
        assert(this->predecessors().size() == operands_.size());
        return operands_;
    }
    std::span<const ArrayNode* const> operands() const {
        assert(this->predecessors().size() == operands_.size());
        return operands_;
    }

    void propagate(State& state) const override {
        auto* state_ptr = this->template data_ptr_<BinaryOpState_>(state);

        if (this->dynamic()) {
            assert(false and "not yet implemented");
        }

        const auto& broadcaster = state_ptr->broadcaster;
        const auto& shape = state_ptr->shape;

        auto& buffer = state_ptr->buffer;
        auto& seen = state_ptr->seen;
        assert(buffer.size() == seen.size());

        auto& diff = state_ptr->diff;

        // From that, we can get the views into our predecessors of the size we want
        const auto [lhs_view, rhs_view] = broadcaster.views(state);
        assert(std::size(lhs_view) == std::size(rhs_view));

        // We previously updates our size, so that should now be consistent
        const ssize_t size = this->size();
        assert(std::ssize(lhs_view) == size);

        // For each predecessor, we go backwards through their diff.
        // TODO: explanation
        for (const auto& [lhs_index, _, lhs] : operands_[0]->diff(state) | std::views::reverse) {
            assert(false and "not yet implemtned");
            // std::cout << lhs_index << " lhs index\n";
            // std::cout << lhs << " lhs value\n";

            // std::ranges::for_each(operand_indices[0], [&](const auto& offset) -> void {
            //     const ssize_t index = lhs_index + offset;

            //     std::cout << offset << " offset\n";
            //     std::cout << index << " index\n";

            //     if (size <= index) return;
            //     if (seen[index]) return;
            //     seen[index] += 1;

            //     assert(lhs_view[index] == lhs);  // it was indeed the last word

            //     const double value = op(lhs, rhs_view[index]);

            //     if (buffer[index] == value) return;  // net no change

            //     diff.emplace_back(index, state_ptr->buffer[index], value);
            //     buffer[index] = value;
            // });
        }
        for (const auto& [rhs_index, _, rhs] : operands_[1]->diff(state) | std::views::reverse) {
            std::ranges::for_each(
                broadcaster.template reindex<1>(rhs_index), [&](const auto& index) -> void {
                    if (size <= index) return;
                    if (seen[index]) return;
                    seen[index] += 1;

                    assert(rhs_view[index] == rhs);  // it was indeed the last word

                    const double value = op(lhs_view[index], rhs);

                    if (buffer[index] == value) return;  // net no change

                    diff.emplace_back(index, state_ptr->buffer[index], value);
                    buffer[index] = value;
                }
            );
        }

        assert(std::ranges::all_of(seen, [](auto val) { return 0 <= val and val <= 1; }));

        if (state_ptr->diff.size()) Node::propagate(state);
    }

    void revert(State& state) const override {
        assert(false and "not yet implememented");
        // auto* state_ptr = this->template data_ptr_<BinaryOpState_>(state);

        // const ssize_t previous_size =
        //     this->ndim() ? (state_ptr->previous_num_rows * state_ptr->row_size) : 1;

        // // Resize the buffer and then revert the changes
        // state_ptr->buffer.resize(previous_size, Update::nothing);
        // state_ptr->seen.resize(previous_size, false);
        // for (const auto& [index, old, val] : state_ptr->diff | std::views::reverse) {
        //     if (previous_size <= index) continue;  // already resized
        //     assert(false and "not yet implemented");
        // }

        // // We already cleared the seen
        // assert(std::ranges::none_of(state_ptr->seen, [](auto x) { return x; }));

        // // clear the diff
        // state_ptr->diff.clear();

        // // adjust the shape back
        // if (this->ndim()) {
        //     state_ptr->shape[0] = state_ptr->previous_num_rows;
        // }

        // // previous_num_rows doesn't need to change

        // // row_size is const
    }

    std::span<const ssize_t> shape(const State& state) const override {
        if (not this->dynamic()) return this->shape();

        assert(false and "not yet implememented");

        // const auto* state_ptr = this->template data_ptr_<BinaryOpState_>(state);
        // return std::span<const ssize_t>(state_ptr->shape.get(), this->ndim());
    }
    using ArrayOutputMixin<EqualityMixin<ArrayNode, BinaryOpNode<BinaryOp>>>::shape;

    ssize_t size(const State& state) const override {
        if (not this->dynamic()) return this->size();

        assert(false and "not yet implemented");

        // const auto* state_ptr = this->template data_ptr_<BinaryOpState_>(state);

        // // double check that the shape and size are consistent
        // assert(
        //     std::ranges::fold_left(this->shape(state), ssize_t{1}, op::multiply{}) ==
        //     static_cast<ssize_t>(state_ptr->buffer.size())
        // );

        // return state_ptr->buffer.size();
    }
    using ArrayOutputMixin<EqualityMixin<ArrayNode, BinaryOpNode<BinaryOp>>>::size;

    ssize_t size_diff(const State& state) const override {
        if (not this->dynamic()) return 0;

        assert(false and "not yet implemented");

        // const auto* state_ptr = this->template data_ptr_<BinaryOpState_>(state);

        // const ssize_t current_size = state_ptr->buffer.size();
        // const ssize_t previous_size = state_ptr->row_size * state_ptr->previous_num_rows;

        // return current_size - previous_size;
    }

    SizeInfo sizeinfo() const override { return sizeinfo_; }

    const BinaryOp op;

 private:
    // At some point we'll want to promote this to the main namespace.
    // We can also have it take N arrays rather than just two, etc etc.
    class broadcast_ {
     public:
        static constexpr ssize_t num_arrays = 2;

        broadcast_(const Array* lhs_ptr, const Array* rhs_ptr) :
            operands_{lhs_ptr, rhs_ptr},
            shape_(broadcast_shapes(*lhs_ptr, *rhs_ptr)),
            statefull_shape_(shape_),
            operand_strides_{
                broadcast_strides_to(lhs_ptr->shape(), lhs_ptr->strides(), shape_),
                broadcast_strides_to(rhs_ptr->shape(), rhs_ptr->strides(), shape_)
            },
            operand_index_offsets_{
                broadcast_index_to(lhs_ptr->shape(), shape_),  // TODO: test (1,) to (n,) arrays
                broadcast_index_to(rhs_ptr->shape(), shape_)
            } {}

        template <ssize_t Axis>
        auto iter(const State& state) const {
            static_assert(0 <= Axis and Axis < decltype(operands_){}.size());

            const Array* array_ptr = operands_[Axis];

            // To handle the 0dim case, we have to do the same trick as we do in
            // Array::begin(). We should really change the BufferIterator class to
            // handle this case.
            if (shape_.empty()) {
                static constexpr ssize_t shape = 1;
                static constexpr ssize_t strides = 0;  // doesn't matter
                return Array::const_iterator(array_ptr->buff(state), 1, &shape, &strides);
            }

            return Array::const_iterator(
                array_ptr->buff(state), shape(state), operand_strides_[Axis]
            );
        }

        auto iters(const State& state) const {
            return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return std::array{iter<Is>(state)...};
            }(std::make_index_sequence<num_arrays>{});
        }

        ssize_t ndim() const { return std::ssize(shape_); }

        template <ssize_t Axis>
        std::ranges::range auto reindex(ssize_t array_index) const {
            static_assert(0 <= Axis and Axis < decltype(operands_){}.size());

            static_assert(num_arrays == 2, "not implement for num_arrays != 2");

            if (operands_[Axis]->size() == 1) {
                assert(false and "not yet implemented");
            }

            if (operands_[Axis]->dynamic()) {
                assert(false and "not yet implemented");
            }

            const ssize_t base = this->base_<Axis>(array_index);

            return operand_index_offsets_[Axis] |
                   std::views::transform([&base](ssize_t off) -> ssize_t { return off + base; });
        }

        std::span<const ssize_t> shape() const { return shape_; }

        std::span<const ssize_t> shape(const State&) const {
            // See note on statefull_shape_ attribute.
            return statefull_shape_;
        }

        void update_shape(const State& state) {
            if (shape_.empty()) return;  // 0dim, nothing to do
            if (0 <= shape_[0]) return;  // fixed shape, nothing to do

            assert(false and "not yet implemented");
        }

        auto views(const State& state) const {
            return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return std::array{std::ranges::subrange(iter<Is>(state), std::default_sentinel)...};
            }(std::make_index_sequence<num_arrays>{});
        }

     private:
        template <ssize_t Axis>
        ssize_t base_(ssize_t index) const {
            assert(false and "not yet implemented");
        }

        std::array<const Array*, num_arrays> operands_;

        std::vector<ssize_t> shape_;

        // This is one of the main warts that is keeping us from promoting this
        // class to the main namespace. Ideally we could make the `broadcast`
        // class `const` and read this off of our predecessors. But
        // iterators don't own their shapes so we need something for them to
        // reference. The most convenient solution would be allowing iterators
        // to hold their own shapes. Needs more thought.
        std::vector<ssize_t> statefull_shape_;

        std::array<std::vector<ssize_t>, num_arrays> operand_strides_;

        std::array<std::vector<ssize_t>, num_arrays> operand_index_offsets_;
    };

    struct BinaryOpState_ : NodeStateData {
        BinaryOpState_(
            broadcast_&& broadcaster,
            std::vector<ssize_t>&& shape,
            std::vector<double>&& buffer
        ) :
            broadcaster(std::move(broadcaster)), buffer(std::move(buffer)) {}

        broadcast_ broadcaster;

        std::vector<ssize_t> shape;
        ssize_t previous_num_rows = shape.empty() ? -1 : shape[0];
        const ssize_t row_size =
            std::ranges::fold_left(shape | std::views::drop(1), ssize_t{1}, op::multiply{});

        std::vector<double> buffer;
        std::vector<signed char> seen = std::vector<signed char>(buffer.size(), 0);

        std::vector<Update> diff = {};
    };

    static interval<double> calculate_domain_(
        BinaryOp& op,
        const Array* lhs_ptr,
        const Array* rhs_ptr
    ) {
        std::variant<interval<double>, interval<long long>> lhs_enclosure;
        if (lhs_ptr->integral()) {
            lhs_enclosure = interval<long long>(lhs_ptr->min(), lhs_ptr->max());
        } else {
            lhs_enclosure = interval<double>(lhs_ptr->min(), lhs_ptr->max());
        }

        std::variant<interval<double>, interval<long long>> rhs_enclosure;
        if (rhs_ptr->integral()) {
            rhs_enclosure = interval<long long>(rhs_ptr->min(), rhs_ptr->max());
        } else {
            rhs_enclosure = interval<double>(rhs_ptr->min(), rhs_ptr->max());
        }

        return std::visit(
            [&op](const auto& lhs, const auto& rhs) -> interval<double> { return op(lhs, rhs); },
            lhs_enclosure,
            rhs_enclosure
        );
    }

    static bool calculate_integral_(BinaryOp& op, const Array* lhs_ptr, const Array* rhs_ptr) {
        if (lhs_ptr->integral() and rhs_ptr->integral()) {
            return std::integral<decltype(op(int{}, int{}))>;
        } else if (lhs_ptr->integral()) {
            return std::integral<decltype(op(int{}, double{}))>;
        } else if (rhs_ptr->integral()) {
            return std::integral<decltype(op(double{}, int{}))>;
        } else {
            return std::integral<decltype(op(double{}, double{}))>;
        }
    }

    // Determine the size of the resulting array.
    // Raises an error if they are incompatible.
    // Requires that lhs and rhs be broadcastable.
    static SizeInfo calculate_sizeinfo_(
        std::span<const ssize_t> shape,
        const Array* lhs_ptr,
        const Array* rhs_ptr
    ) {
        // If the shape isn't dynamic, then the size if fixed and so is SizeInfo
        if (shape.empty() or 0 <= shape[0]) {
            return SizeInfo(std::ranges::fold_left(shape, ssize_t{1}, op::multiply{}));
        }

        assert(false and "not yet implemented");

        // if (std::ranges::equal(lhs_ptr->shape(), rhs_ptr->shape()) and
        //     lhs_ptr->sizeinfo().substitute(100) == rhs_ptr->sizeinfo().substitute(100)) {
        //     // We have compatible shapes and matching sizeinfo! So we can get our sizeinfo from
        //     // either side
        //     return lhs_ptr->sizeinfo();
        // } else if (lhs_ptr->size() == 1) {
        //     // we're broadcasting lhs to rhs, so we get our size from rhs
        //     return rhs_ptr->sizeinfo();
        // } else if (rhs_ptr->size() == 1) {
        //     // we're broadcasting rhs to lhs, so we get our size from lhs
        //     return lhs_ptr->sizeinfo();
        // } else {
        //     throw std::invalid_argument("arrays must have the same shape or one must be size 1");
        // }
    }

    // // TODO
    // static std::ranges::random_access_range auto operand_view_(
    //     const double* buff,
    //     std::span<const ssize_t> shape,
    //     std::span<const ssize_t> strides
    // ) {
    //     assert(shape.size() == strides.size());

    //     // To handle the 0dim case, we have to do the same trick as we do in
    //     // Array::begin(). We should really change the BufferIterator class to
    //     // handle this case.
    //     if (shape.empty()) {
    //         static constexpr ssize_t shape = 1;
    //         static constexpr ssize_t strides = 0;  // doesn't matter
    //         auto it = Array::const_iterator(buff, 1, &shape, &strides);
    //         return std::ranges::subrange(std::move(it), std::default_sentinel);
    //     }

    //     assert(0 <= shape[0] and "must use state-dependent shape");

    //     auto it = Array::const_iterator(buff, shape, strides);
    //     return std::ranges::subrange(std::move(it), std::default_sentinel);
    // }

    void replace_predecessor_(ssize_t previous_index, Node* node_ptr) override {
        Node::replace_predecessor_(previous_index, node_ptr);

        assert(0 <= previous_index and previous_index < 2);
        operands_[previous_index] = dynamic_cast<ArrayNode*>(node_ptr);
        assert(operands_[previous_index] != nullptr);
    }

    std::array<ArrayNode*, 2> operands_;

    interval<double> domain_;  // the range of possible output values
    bool integral_;            // whether the node will always output integral values

    const SizeInfo sizeinfo_;
};

// We follow NumPy naming convention rather than C++ to distinguish between
// binary operations and reduce operations.
// https://numpy.org/doc/stable/reference/routines.math.html
using AddNode = BinaryOpNode<op::add>;
extern template class BinaryOpNode<op::add>;

using AndNode = BinaryOpNode<op::logical_and>;
extern template class BinaryOpNode<op::logical_and>;

using DivideNode = BinaryOpNode<op::divide>;
extern template class BinaryOpNode<op::divide>;

using EqualNode = BinaryOpNode<op::equal>;
extern template class BinaryOpNode<op::equal>;

using LessEqualNode = BinaryOpNode<op::less_equal>;
extern template class BinaryOpNode<op::less_equal>;

using MaximumNode = BinaryOpNode<op::maximum>;
extern template class BinaryOpNode<op::maximum>;

using MinimumNode = BinaryOpNode<op::minimum>;
extern template class BinaryOpNode<op::minimum>;

using ModulusNode = BinaryOpNode<op::remainder>;
extern template class BinaryOpNode<op::remainder>;

using MultiplyNode = BinaryOpNode<op::multiply>;
extern template class BinaryOpNode<op::multiply>;

using OrNode = BinaryOpNode<op::logical_or>;
extern template class BinaryOpNode<op::logical_or>;

using SubtractNode = BinaryOpNode<op::subtract>;
extern template class BinaryOpNode<op::subtract>;

using XorNode = BinaryOpNode<op::logical_xor>;
extern template class BinaryOpNode<op::logical_xor>;

}  // namespace dwave::optimization
