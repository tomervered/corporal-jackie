# Third-party add-ons and content

Corporal Jackie can use Stremio-compatible add-ons. An add-on is a web service run by someone else. It answers the app's requests with lists of titles, details, and sometimes ways to play a title: web links or torrents.

## Who runs them

Add-ons are not run, checked or endorsed by this project, and this project is not affiliated with Stremio. Two add-ons come installed, and the first-run screen has a switch that removes both: WikiMaze, a catalog add-on run by the app's author with data from Wikidata (CC0) and TVmaze (CC BY-SA 4.0), and Public Domain Movies, an add-on on Stremio's server (`caching.stremio.net`) that lists films it describes as public domain, with torrents. This project has not checked the copyright status of each film in that list. Every other add-on is one you install yourself by pasting its address on the Add-ons page.

## What happens when you install one

Before the app contacts a new add-on, it shows the add-on's address and this notice:

> Add-ons are run by third parties, not by Corporal Jackie. The app will send this one the titles you open and search for, and it will see your IP address. Nobody has checked whether the streams it lists are licensed: use only those you have the right to watch.

Nothing is sent to the add-on until you press Install. Importing a backup lists the add-ons it would add and asks before adding them.

## What the app shows

Each way to play is listed with the name of the add-on that gave it and its type: torrent, web or file. The app sorts the list by quality, seeders and size. That order says nothing about whether a source is licensed, and the app labels no source as legal or licensed.

## Your control

You can remove an add-on on the Add-ons page at any time, or hide its results in a stream list. What each add-on receives is listed in [PRIVACY.md](PRIVACY.md).
