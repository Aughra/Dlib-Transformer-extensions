// L'empreinte SHA-256 du moteur (engine/image_input.cpp), comparée aux valeurs de référence de la norme
// FIPS 180-2, et l'empreinte d'une image, qui doit suivre sa taille et ses pixels (fork pour macOS).

#include "../engine/image_input.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void check(bool ok, const char* what)
    {
        if (!ok)
        {
            std::printf("FAILED: %s\n", what);
            ++failures;
        }
    }

    std::string hex(const std::array<uint8_t, 32>& h)
    {
        static const char digits[] = "0123456789abcdef";
        std::string s;
        for (uint8_t b : h)
        {
            s += digits[b >> 4];
            s += digits[b & 15];
        }
        return s;
    }

    std::array<uint8_t, 32> of(const std::string& s)
    {
        return gulliver::sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
}

int main()
{
    check(hex(of("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty input");
    check(hex(of("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
    check(hex(of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))
        == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "two blocks");
    check(hex(of(std::string(1000000, 'a'))) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "one million a");

    // L'empreinte d'une image : la largeur et la hauteur (8 octets chacune, petit-boutiste), puis les pixels.
    gulliver::rgb_image a;
    a.width = 2;
    a.height = 1;
    a.pixels = { 1, 2, 3, 4, 5, 6 };
    std::vector<uint8_t> bytes(16, 0);
    bytes[0] = 2;
    bytes[8] = 1;
    bytes.insert(bytes.end(), a.pixels.begin(), a.pixels.end());
    check(gulliver::image_digest(a) == gulliver::sha256(bytes.data(), bytes.size()), "image digest = size and pixels");
    gulliver::rgb_image b = a;
    b.width = 1;
    b.height = 2;
    check(gulliver::image_digest(a) != gulliver::image_digest(b), "same pixels, other shape");
    b = a;
    b.pixels[5] = 7;
    check(gulliver::image_digest(a) != gulliver::image_digest(b), "one pixel changed");

    if (failures == 0) std::printf("test_image_input: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
