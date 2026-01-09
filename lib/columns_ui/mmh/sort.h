#pragma once

namespace mmh {
using PermutationBase = pfc::list_t<t_size>;

class Permutation {
    using Storage = std::vector<size_t>;

public:
    Permutation() = default;
    Permutation(Storage::size_type size) { resize(size); }

    void resize(Storage::size_type size)
    {
        m_data.resize(size);
        std::iota(m_data.begin(), m_data.end(), Storage::value_type{0});
    }

    [[nodiscard]] Storage::size_type size() const { return m_data.size(); }
    [[nodiscard]] Storage::value_type* data() { return m_data.data(); }

    Storage::value_type& operator[](Storage::size_type index) noexcept { return m_data[index]; }
    Storage::value_type operator[](Storage::size_type index) const noexcept { return m_data[index]; }

    decltype(std::declval<Storage>().begin()) begin() noexcept { return m_data.begin(); }
    decltype(std::declval<Storage>().end()) end() noexcept { return m_data.end(); }
    decltype(std::declval<Storage>().rbegin()) rbegin() noexcept { return m_data.rbegin(); }
    decltype(std::declval<Storage>().rend()) rend() noexcept { return m_data.rend(); }
    decltype(std::declval<Storage>().cbegin()) cbegin() const noexcept { return m_data.cbegin(); }
    decltype(std::declval<Storage>().cend()) cend() const noexcept { return m_data.cend(); }
    decltype(std::declval<Storage>().crbegin()) crbegin() const noexcept { return m_data.crbegin(); }
    decltype(std::declval<Storage>().crend()) crend() const noexcept { return m_data.crend(); }

    Permutation invert() const
    {
        Permutation inverted;
        inverted.m_data.resize(size());

        for (Storage::size_type i{}; i < size(); ++i)
            inverted.m_data[m_data[i]] = i;

        return inverted;
    }

    Permutation reverse() const
    {
        Permutation reversed;
        reversed.m_data.resize(size());

        for (Storage::size_type i{}, size_ = size(); i < size_; ++i)
            reversed.m_data[m_data[size_ - i - 1]] = i;

        return reversed;
    }

private:
    Storage m_data;
};

template <typename List>
void destructive_reorder(List& items, mmh::Permutation& perm)
{
    for (size_t i = 1; i < perm.size(); ++i) {
        t_size current = i;
        t_size next = perm[i];
        while (next != i) {
            std::swap(items[current], items[next]);
            perm[current] = current;
            current = next;
            next = perm[next];
        }
        perm[current] = current;
    }
}

template <typename List, typename Comparator>
class IndexComparatorWrapper {
    Comparator& m_compare;
    List& m_list;
    bool m_reverse;
    bool m_stabilise;

public:
    bool operator()(const t_size& item1, const t_size& item2) const
    {
        int diff = m_compare(m_list[item1], m_list[item2]);
        if (m_stabilise && !diff)
            return item1 < item2;
        return m_reverse ? diff > 0 : diff < 0;
    }
    IndexComparatorWrapper(List& p_list, Comparator& p_compare, bool b_reverse, bool stabilise = false)
        : m_compare{p_compare}
        , m_list{p_list}
        , m_reverse{b_reverse}
        , m_stabilise{stabilise}
    {
    }
};

template <typename Comparator>
class ComparatorWrapper {
    Comparator& m_compare;
    bool m_reverse;

public:
    template <typename Item>
    bool operator()(const Item& item1, const Item& item2) const
    {
        int diff = m_compare(item1, item2);
        return m_reverse ? diff > 0 : diff < 0;
    }
    ComparatorWrapper(Comparator& p_compare, bool b_reverse) : m_compare{p_compare}, m_reverse{b_reverse} {}
};

template <typename List, typename Comparator>
void sort_get_permutation(List&& p_items, Permutation& p_out, Comparator&& p_compare, bool stabilise,
    bool b_reverse = false, bool allow_parallelisation = false, size_t parallel_chunk_size = 512)
{
    const t_size psize = get_container_size(p_out);
    t_size* out_ptr = p_out.data();
    if (allow_parallelisation && psize >= parallel_chunk_size) {
        // C++17 has parallel sort, but it is not implemented in VC2017 yet.
        IndexComparatorWrapper<List, Comparator> p_context(p_items, p_compare, b_reverse, stabilise);
        concurrency::parallel_buffered_sort(out_ptr, out_ptr + psize, p_context, parallel_chunk_size);
    } else {
        IndexComparatorWrapper<List, Comparator> p_context(p_items, p_compare, b_reverse);
        if (stabilise)
            std::stable_sort(out_ptr, out_ptr + psize, p_context);
        else
            std::sort(out_ptr, out_ptr + psize, p_context);
    }
}

template <typename List, typename Comparator>
void in_place_sort(List&& items, Comparator&& comparator, bool stabilise, bool reverse = false,
    bool allow_parallelisation = false, size_t parallel_chunk_size = 512)
{
    t_size psize = get_container_size(items);
    auto* out_ptr = get_container_data(items);
    ComparatorWrapper<Comparator> p_context(comparator, reverse);
    if (!stabilise && allow_parallelisation && psize >= parallel_chunk_size) {
        // C++17 has parallel sort, but it is not implemented in VC2017 yet.
        concurrency::parallel_buffered_sort(out_ptr, out_ptr + psize, p_context, parallel_chunk_size);
    } else {
        if (stabilise)
            std::stable_sort(out_ptr, out_ptr + psize, p_context);
        else
            std::sort(out_ptr, out_ptr + psize, p_context);
    }
}

template <typename List, typename Comparator>
void single_reordering_sort(List&& items, Comparator&& comparator, bool stabilise, bool reverse = false,
    bool allow_parallelisation = false, size_t parallel_chunk_size = 512)
{
    t_size size = get_container_size(items);
    Permutation perm(size);
    sort_get_permutation(items, perm, comparator, stabilise, reverse, allow_parallelisation, parallel_chunk_size);
    destructive_reorder(items, perm);
}

template <typename t_item, template <typename> class t_alloc, typename t_compare>
void remove_duplicates(pfc::list_t<t_item, t_alloc>& p_handles, t_compare p_compare)
{
    t_size count = p_handles.get_count();
    if (count > 0) {
        t_item* p_list = p_handles.get_ptr();
        pfc::bit_array_bittable mask(count);
        Permutation order(count);

        sort_get_permutation(p_list, order, p_compare, false, false);

        t_size n;
        bool found = false;
        for (n = 0; n < count - 1; n++) {
            if (p_list[order[n]] == p_list[order[n + 1]]) {
                found = true;
                mask.set(order[n + 1], true);
            }
        }

        if (found)
            p_handles.remove_mask(mask);
    }
}

template <typename t_container, typename t_compare, typename t_param>
class PartialBSearchCallback : public pfc::bsearch_callback {
public:
    int test(t_size p_index) const override
    {
        return m_compare(m_container[m_base + p_index], m_param) * (m_is_reversed ? -1 : 1);
    }
    PartialBSearchCallback(const t_container& p_container, t_compare p_compare, const t_param& p_param, t_size base,
        bool is_reversed = false)
        : m_container(p_container)
        , m_compare(p_compare)
        , m_param(p_param)
        , m_base(base)
        , m_is_reversed(is_reversed)
    {
    }

private:
    const t_container& m_container;
    t_compare m_compare;
    const t_param& m_param;
    t_size m_base;
    bool m_is_reversed;
};

template <typename t_container, typename t_compare, typename t_param>
bool partial_bsearch(t_size p_count, const t_container& p_container, t_compare p_compare, const t_param& p_param,
    t_size base, t_size& p_index, bool is_reversed = false)
{
    t_size index = p_index;
    bool ret = bsearch(p_count,
        PartialBSearchCallback<t_container, t_compare, t_param>(p_container, p_compare, p_param, base, is_reversed),
        index);
    p_index = index + base;
    return ret;
}
} // namespace mmh
