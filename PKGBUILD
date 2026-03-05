# Maintainer: Rowen <rowen@example.com>
pkgname=vb-engine
pkgver=0.1.0
pkgrel=1
pkgdesc="Realtime-safe C++20 sound engine with C ABI for embedding in host apps"
arch=('x86_64')
url="https://github.com/l3afyb0y/VB-Engine"
license=('MIT')
depends=('gcc-libs')
makedepends=('cmake' 'ninja')
source=("${pkgname}::git+${url}.git")
sha256sums=('SKIP')

build() {
  cmake -S "${srcdir}/${pkgname}" -B "${srcdir}/build" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DVB_ENGINE_BUILD_TESTS=ON \
    -DVB_ENGINE_BUILD_BENCHMARKS=ON
  cmake --build "${srcdir}/build"
}

check() {
  ctest --test-dir "${srcdir}/build" --output-on-failure
}

package() {
  DESTDIR="${pkgdir}" cmake --install "${srcdir}/build"
  install -Dm644 "${srcdir}/${pkgname}/LICENSE" "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"
  install -Dm644 "${srcdir}/${pkgname}/src/third_party/README.md" "${pkgdir}/usr/share/doc/${pkgname}/THIRD_PARTY.md"
  install -Dm644 "${srcdir}/${pkgname}/README.md" "${pkgdir}/usr/share/doc/${pkgname}/README.md"
  install -Dm644 "${srcdir}/${pkgname}/docs/README.md" "${pkgdir}/usr/share/doc/${pkgname}/docs-index.md"
  install -Dm644 "${srcdir}/${pkgname}/docs/developer-app-integration.md" "${pkgdir}/usr/share/doc/${pkgname}/developer-app-integration.md"
  install -Dm644 "${srcdir}/${pkgname}/docs/licensing-compliance.md" "${pkgdir}/usr/share/doc/${pkgname}/licensing-compliance.md"
  install -Dm644 "${srcdir}/${pkgname}/docs/limitations.md" "${pkgdir}/usr/share/doc/${pkgname}/limitations.md"
}
