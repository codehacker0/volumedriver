AC_LOCAL_AMFLAGS = -I ../../../m4

if USE_MD5_HASH
BASE_NAME=volumedriver-pitreplication
else
BASE_NAME=volumedriver-no-dedup-pitreplication
endif

DEBIAN_LOCATION = $(prefix)/debian
ARTEFACT = ../$(BASE_NAME)_@VD_MAJOR_VERSION@.@VD_MINOR_VERSION@.@VD_PATCH_VERSION@-@VD_EXTRA_VERSION@_amd64.deb
ARTEFACT_DBGSYM = ../$(BASE_NAME)-dbgsym_@VD_MAJOR_VERSION@.@VD_MINOR_VERSION@.@VD_PATCH_VERSION@-@VD_EXTRA_VERSION@_amd64.ddeb

noinst_DATA = debian/control-template \
	debian/source/format \
	debian/copyright \
	debian/changelog \
	debian/rules \
	debian/compat \
	debian/$(BASE_NAME)-head.install \
	add-python-dirs.sh

.PHONY : debian/control debian/$(BASE_NAME).install $(ARTEFACT)

debian/control : debian/control-template
if USE_MD5_HASH
	cp debian/control-template debian/control
else
	cp debian/control-template-no-dedup debian/control
endif
	../update-buildinfo.sh

debian/$(BASE_NAME).install: debian/$(BASE_NAME)-head.install
	cp debian/$(BASE_NAME)-head.install debian/$(BASE_NAME).install
	./add-python-dirs.sh

clean-local:
	-rm -rf ../$(BASE_NAME)_*


$(ARTEFACT) : debian/control debian/$(BASE_NAME).install
	dpkg-buildpackage -us -uc

install-data-local: $(ARTEFACT)
	$(INSTALL) -d $(DEBIAN_LOCATION)
	$(INSTALL) $(ARTEFACT) $(DEBIAN_LOCATION)
	$(INSTALL) $(ARTEFACT_DBGSYM) $(DEBIAN_LOCATION)
