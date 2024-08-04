
int
xmakeglyphfontspecs(XftGlyphFontSpec *specs, const Glyph *glyphs, int len, int x, int y)
{
	float winx = win.hborderpx + x * win.cw, winy = win.vborderpx + y * win.ch, xp, yp;
	ushort mode, prevmode = USHRT_MAX;
	Font *font = &dc.font;
	int frcflags = FRC_NORMAL;
	float runewidth = win.cw;
	Rune rune;
	FT_UInt glyphidx;
	FcResult fcres;
	FcPattern *fcpattern, *fontpattern;
	FcFontSet *fcsets[] = { NULL };
	FcCharSet *fccharset;
	int i, f, length = 0, start = 0, numspecs = 0;
	float cluster_xp = xp, cluster_yp = yp;
	HbTransformData shaped = { 0 };

	/* Initial values. */
	mode = prevmode = glyphs[0].mode & ~ATTR_WRAP;
	xresetfontsettings(mode, &font, &frcflags);

	for (i = 0, xp = winx, yp = winy + font->ascent; i < len; ++i) {
		mode = glyphs[i].mode & ~ATTR_WRAP;

		/* Skip dummy wide-character spacing. */
		if (mode & ATTR_WDUMMY && i < (len - 1))
			continue;

		if (
			prevmode != mode
			|| ATTRCMP(glyphs[start], glyphs[i])
			|| selected(x + i, y) != selected(x + start, y)
			|| i == (len - 1)
		) {
			/* Handle 1-character wide segments and end of line */
			length = i - start;
			if (i == start) {
				length = 1;
			} else if (i == (len - 1)) {
				length = (i - start + 1);
			}
			/* Shape the segment. */
			hbtransform(&shaped, font->match, glyphs, start, length);
			runewidth = win.cw * ((glyphs[start].mode & ATTR_WIDE) ? 2.0f : 1.0f);
			cluster_xp = xp; cluster_yp = yp;
			for (int code_idx = 0; code_idx < shaped.count; code_idx++) {
				int idx = shaped.glyphs[code_idx].cluster;

				if (glyphs[start + idx].mode & ATTR_WDUMMY)
					continue;

				/* Advance the drawing cursor if we've moved to a new cluster */
				if (code_idx > 0 && idx != shaped.glyphs[code_idx - 1].cluster) {
					xp += runewidth;
					cluster_xp = xp;
					cluster_yp = yp;
					runewidth = win.cw * ((glyphs[start + idx].mode & ATTR_WIDE) ? 2.0f : 1.0f);
				}

				if (glyphs[start + idx].mode & ATTR_BOXDRAW) {
					/* minor shoehorning: boxdraw uses only this ushort */
					specs[numspecs].font = font->match;
					specs[numspecs].glyph = boxdrawindex(&glyphs[start + idx]);
					specs[numspecs].x = xp;
					specs[numspecs].y = yp;
					numspecs++;
				} else if (shaped.glyphs[code_idx].codepoint != 0) {
					/* If symbol is found, put it into the specs. */
					specs[numspecs].font = font->match;
					specs[numspecs].glyph = shaped.glyphs[code_idx].codepoint;
					specs[numspecs].x = cluster_xp + (short)(shaped.positions[code_idx].x_offset / 64.);
					specs[numspecs].y = cluster_yp - (short)(shaped.positions[code_idx].y_offset / 64.);
					cluster_xp += shaped.positions[code_idx].x_advance / 64.;
					cluster_yp += shaped.positions[code_idx].y_advance / 64.;
					numspecs++;
				} else {
					/* If it's not found, try to fetch it through the font cache. */
					rune = glyphs[start + idx].u;
					for (f = 0; f < frclen; f++) {
						glyphidx = XftCharIndex(xw.dpy, frc[f].font, rune);
						/* Everything correct. */
						if (glyphidx && frc[f].flags == frcflags)
							break;
						/* We got a default font for a not found glyph. */
						if (!glyphidx && frc[f].flags == frcflags
								&& frc[f].unicodep == rune) {
							break;
						}
					}

					/* Nothing was found. Use fontconfig to find matching font. */
					if (f >= frclen) {
						if (!font->set)
							font->set = FcFontSort(0, font->pattern,
																		 1, 0, &fcres);
						fcsets[0] = font->set;

						/*
						 * Nothing was found in the cache. Now use
						 * some dozen of Fontconfig calls to get the
						 * font for one single character.
						 *
						 * Xft and fontconfig are design failures.
						 */
						fcpattern = FcPatternDuplicate(font->pattern);
						fccharset = FcCharSetCreate();

						FcCharSetAddChar(fccharset, rune);
						FcPatternAddCharSet(fcpattern, FC_CHARSET,
								fccharset);
						FcPatternAddBool(fcpattern, FC_SCALABLE, 1);

						FcConfigSubstitute(0, fcpattern,
								FcMatchPattern);
						FcDefaultSubstitute(fcpattern);

						fontpattern = FcFontSetMatch(0, fcsets, 1,
								fcpattern, &fcres);

						/* Allocate memory for the new cache entry. */
						if (frclen >= frccap) {
							frccap += 16;
							frc = xrealloc(frc, frccap * sizeof(Fontcache));
						}

						frc[frclen].font = XftFontOpenPattern(xw.dpy,
								fontpattern);
						if (!frc[frclen].font)
							die("XftFontOpenPattern failed seeking fallback font: %s\n",
								strerror(errno));
						frc[frclen].flags = frcflags;
						frc[frclen].unicodep = rune;

						glyphidx = XftCharIndex(xw.dpy, frc[frclen].font, rune);

						f = frclen;
						frclen++;

						FcPatternDestroy(fcpattern);
						FcCharSetDestroy(fccharset);
					}

					specs[numspecs].font = frc[f].font;
					specs[numspecs].glyph = glyphidx;
					specs[numspecs].x = (short)xp;
					specs[numspecs].y = (short)yp;
					numspecs++;
      }
			}

			/* Cleanup and get ready for next segment. */
			hbcleanup(&shaped);
			start = i;
		
			/* Determine font for glyph if different from previous glyph. */
			if (prevmode != mode) {
				prevmode = mode;
				xresetfontsettings(mode, &font, &frcflags);
				yp = winy + font->ascent;
			}
	}
		
	hbcleanup(&shaped);
	return numspecs;
}
