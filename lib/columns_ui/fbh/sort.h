#pragma once

namespace fbh {
template <typename TList>
void sort_metadb_handle_list_by_format_get_permutation(TList&& tracks, mmh::Permutation& order,
    const service_ptr_t<titleformat_object>& script, titleformat_hook* hook, bool stablise = false,
    bool reverse = false)
{
    const auto size = tracks.get_count();
    assert(size == order.size());

    pfc::array_t<pfc::array_t<WCHAR>> data;
    data.set_size(size);

    const auto metadb_v2_api = metadb_v2::tryGet();

    if (metadb_v2_api.is_valid()) {
        metadb_v2_api->queryMultiParallel_(
            tracks, [&tracks, &script, &data](size_t index, const metadb_v2::rec_t& rec) {
                if (!rec.info.is_valid()) {
                    data[index].set_size(1);
                    data[index].fill(0);
                    return;
                }

                metadb_handle_v2::ptr track;
                track &= tracks[index];

                std::string title;
                mmh::StringAdaptor interop_title(title);
                track->formatTitle_v2(rec, nullptr, interop_title, script, nullptr);

                data[index].set_size(pfc::stringcvt::estimate_utf8_to_wide_quick(title.c_str(), title.size()));
                pfc::stringcvt::convert_utf8_to_wide_unchecked(data[index].get_ptr(), title.c_str());
            });

    } else {
        concurrency::parallel_for(size_t{0}, size, [&](size_t index) {
            std::string title;
            mmh::StringAdaptor interop_title(title);
            tracks[index]->format_title(hook, interop_title, script, nullptr);
            data[index].set_size(pfc::stringcvt::estimate_utf8_to_wide_quick(title.c_str(), title.size()));
            pfc::stringcvt::convert_utf8_to_wide_unchecked(data[index].get_ptr(), title.c_str());
        });
    }

    const auto comparator = [](const pfc::array_t<WCHAR>& elem1, const pfc::array_t<WCHAR>& elem2) {
        return StrCmpLogicalW(elem1.get_ptr(), elem2.get_ptr());
    };

    mmh::sort_get_permutation(data, order, comparator, stablise, reverse, true);
}

template <typename TList>
void sort_metadb_handle_list_by_format(TList&& tracks, const service_ptr_t<titleformat_object>& script,
    titleformat_hook* hook, bool stablise = false, bool reverse = false)
{
    mmh::Permutation perm(tracks.get_count());
    sort_metadb_handle_list_by_format_get_permutation(tracks, perm, script, hook, stablise, reverse);
    tracks.reorder(perm.data());
}

template <template <typename> class t_alloc>
void metadb_handle_list_remove_duplicates(metadb_handle_list_t<t_alloc>& p_handles)
{
    mmh::remove_duplicates(p_handles, pfc::compare_t<metadb_handle_ptr, metadb_handle_ptr>);
}
} // namespace fbh
