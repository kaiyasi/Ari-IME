# Maintainer: Kaiyasi <zengcode0315@gmail.com>
pkgname=fcitx5-ari-ime
pkgver=0.2.0
pkgrel=1
pkgdesc="知字 (Ari IME): Fcitx5 mixed Bopomofo/English input without mode switching"
arch=('x86_64')
url="https://github.com/kaiyasi/Ari-IME"
license=('GPL-3.0-or-later')
depends=('fcitx5' 'libchewing')
makedepends=('cmake' 'extra-cmake-modules' 'gcc')
# For a release, point source at a tagged tarball and set the checksum:
#   source=("$pkgname-$pkgver.tar.gz::$url/archive/refs/tags/v$pkgver.tar.gz")
#   sha256sums=('SKIP')
source=()
sha256sums=()

build() {
    cd "$srcdir/.."
    cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTING=OFF
    cmake --build build
}

package() {
    cd "$srcdir/.."
    DESTDIR="$pkgdir" cmake --install build
}
