#include "storage_engine/lsm_tree.hpp"

#include <iostream>
#include <numbers>

namespace {
void test_string_db() {
    std::cout << "--- TEST 1: String Key-Value Testi ---" << '\n';

    DB<std::string, std::string> db;

    // Memtable'a veri ekleme
    db.put("user_1", "Ahmet");
    db.put("user_2", "Mehmet");
    db.put("user_3", "Ayse");

    // Bellekten okuma kontrolü
    const std::string* val = db.get("user_2");
    if (val != nullptr) {
        std::cout << "Memtable'dan Okunan (user_2): " << *val << '\n';
    }

    // Memtable'ı diskteki SSTable'a döküyoruz (Flush)
    std::cout << "SSTable diske yaziliyor..." << '\n';
    auto sstable = db.write_sstable("users_data.bin");

    if (sstable) {
        std::cout << "SSTable basariyla yazildi. Simdi diskten arama yapiliyor..." << '\n';

        // Diskten okuma simülasyonu
        auto res1 = sstable->get("user_3");
        auto res2 = sstable->get("user_not_exist");

        if (res1) {
            std::cout << "SSTable'dan Bulundu (user_3): " << *res1 << '\n';
        }
        if (!res2) {
            std::cout << "SSTable'da Bulunamadi (user_not_exist): Dogru Davranis!" << '\n';
        }
    } else {
        std::cerr << "SSTable diske yazilamadi!" << '\n';
    }
    std::cout << '\n';
}

void test_primitive_db() {
    std::cout << "--- TEST 2: İlkel Tip (int, double) Testi ---" << '\n';

    DB<int, double> db;
    db.put(42, std::numbers::pi);
    db.put(100, 99.9);

    std::cout << "SSTable diske yaziliyor..." << '\n';
    auto sstable = db.write_sstable("metrics_data.bin");

    if (sstable) {
        auto res = sstable->get(42);
        if (res) {
            std::cout << "SSTable'dan Bulundu (Key 42): " << *res << '\n';
        }
    }
    std::cout << '\n';
}
} // namespace

int main() {
    test_string_db();
    test_primitive_db();
    return 0;
}
