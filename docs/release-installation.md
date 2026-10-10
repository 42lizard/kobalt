## Installation

Download the complete ZIP package for your Kobo firmware:

{{package_links}}

1. Connect your Kobo over USB and extract the ZIP package on your computer. Show hidden files so you can see `.adds` and `.kobo`.
2. Copy the complete `.adds` folder to the root of the Kobo drive. Merge it with the existing folder and replace matching package files. Keep other apps, settings and reading data.
3. Copy the complete `.kobo` folder from the extracted package to the same drive root. Merge it with the existing folder.
4. Eject the Kobo safely, disconnect USB and let it install the update.

Copy every file supplied in the ZIP, keeping its folder path. Copy `.adds` before `.kobo` so the assets are present when the update starts. Do not copy only the `.tgz` file or unpack it yourself.

You can verify the downloads against [SHA256SUMS]({{checksums_url}}). [BUILD-INFO.json]({{build_info_url}}) records the source commit and build details for {{tag}}.
