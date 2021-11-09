pkgname=guid
pkgver=r71.968836b
pkgrel=1
pkgdesc="Create cross-platform GUI dialogs in a breeze for Linux, macOS and Windows"
arch=('i686' 'x86_64')
url="https://github.com/misa-ki/guid"
license=('GPL')
depends=('qt5-base')
makedepends=('git' 'gcc')

pkgver()
{
    printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build()
{
    qmake-qt5 ..
    make
}

package()
{
    install -Dm755 guid -t "$pkgdir/usr/bin"
    ln -s /usr/bin/guid "$pkgdir/usr/bin/guid-askpass"
}
