// Paper export — artboard 12 « Erreurs et reprise », layer "Popover · Modifier le favori" (Cas A).
// get_jsx includeNodeIds:false, 2026-11-08 (+ address-field ring re-read via get_computed_styles).
// Comment c-qdholr: while the URL is invalid, the typed URL text is danger red until it is valid.
// Comment c-mzly9w: caret is red in this state, accent blue otherwise.
// Same header cleanup as the valid popover (no title, absolute X top 8 / right 8).
// Invalid-state deltas vs valid popover (only these differ):
//   text color   #FFFFFF  -> var(--color-browser-danger) = #FF7B6E
//   caret        accent 90% (#3E67FFE6) -> danger 90% (#FF7B6EE6)
//   field ring   inset 1px #FFFFFF1A -> inset 1px #FF7B6E80 (danger 50%)
//   + hint row below the field (warning glyph 14px white 70%, text 14/18 #FFFFFFB3)
(
    <div style={{ backgroundColor: '#171717', borderColor: '#FFFFFF0F', borderRadius: '8px', borderStyle: 'solid', borderWidth: '1px', boxShadow: '#00000066 0px 8px 24px', boxSizing: 'border-box', display: 'flex', flexDirection: 'column', fontSize: '12px', fontSynthesis: 'none', gap: '16px', lineHeight: '16px', MozOsxFontSmoothing: 'grayscale', overflowWrap: 'anywhere', padding: '16px', position: 'relative', WebkitFontSmoothing: 'antialiased', width: '320px' }}>
      <div style={{ boxSizing: 'border-box', display: 'flex', flexDirection: 'column', gap: '6px' }}>
        <div style={{ boxSizing: 'border-box', color: '#FFFFFF80', display: 'inline-block', fontFamily: '"InterDisplay-Medium", "Inter Display", system-ui, sans-serif', fontSize: '14px', fontWeight: 500, letterSpacing: '-0.02em', lineHeight: '18px' }}>
          Nom
        </div>
        <div style={{ alignItems: 'center', backgroundColor: '#FFFFFF14', borderRadius: '6px', boxSizing: 'border-box', display: 'flex', flexShrink: 0, height: '32px', paddingInline: '10px' }}>
          <div style={{ boxSizing: 'border-box', display: 'inline-block' }}>
            <div style={{ boxSizing: 'border-box', color: '#FFFFFF', display: 'inline-block', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '16px', letterSpacing: '-0.01em', lineHeight: '20px' }}>
              Le Monde — International
            </div>
          </div>
        </div>
      </div>
      <div style={{ boxSizing: 'border-box', display: 'flex', flexDirection: 'column', gap: '6px' }}>
        <div style={{ boxSizing: 'border-box', color: '#FFFFFF80', display: 'inline-block', fontFamily: '"InterDisplay-Medium", "Inter Display", system-ui, sans-serif', fontSize: '14px', fontWeight: 500, letterSpacing: '-0.02em', lineHeight: '18px' }}>
          Adresse
        </div>
        <div style={{ alignItems: 'center', backgroundColor: '#FFFFFF14', borderRadius: '6px', boxShadow: '#FF7B6E80 0px 0px 0px 1px inset', boxSizing: 'border-box', display: 'flex', flexShrink: 0, height: '32px', overflow: 'clip', paddingInline: '10px' }}>
          <div style={{ boxSizing: 'border-box', color: 'var(--color-browser-danger)', display: 'inline-block', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '16px', letterSpacing: '-0.01em', lineHeight: '20px', whiteSpace: 'pre' }}>
            lemonde..fr/international
          </div>
          <div style={{ backgroundColor: 'color-mix(var(--color-browser-danger) 90%, transparent)', borderRadius: 'calc(infinity * 1px)', boxSizing: 'border-box', flexShrink: '0', height: '18px', width: '1px' }} />
        </div>
        <div style={{ alignItems: 'start', boxSizing: 'border-box', display: 'flex', gap: '6px' }}>
          <svg width="14" height="14" viewBox="0 0 256 256" xmlns="http://www.w3.org/2000/svg" style={{ flexShrink: '0', marginTop: '2px', opacity: '0.7', display: 'inline-block' }}>
            <path d="M236.8,188.09,149.35,36.22h0a24.76,24.76,0,0,0-42.7,0L19.2,188.09a23.51,23.51,0,0,0,0,23.72A24.35,24.35,0,0,0,40.55,224h174.9a24.35,24.35,0,0,0,21.33-12.19A23.51,23.51,0,0,0,236.8,188.09ZM116,104a12,12,0,0,1,24,0v40a12,12,0,0,1-24,0Zm12,88a16,16,0,1,1,16-16A16,16,0,0,1,128,192Z" fill="#FFFFFF" />
          </svg>
          <div style={{ boxSizing: 'border-box', color: '#FFFFFFB3', display: 'inline-block', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '14px', lineHeight: '18px' }}>
            Adresse non valide. Exemple : lemonde.fr/international
          </div>
        </div>
      </div>
      <div style={{ boxSizing: 'border-box', display: 'flex', flexDirection: 'column', gap: '6px' }}>
        <div style={{ boxSizing: 'border-box', color: '#FFFFFF80', display: 'inline-block', fontFamily: '"InterDisplay-Medium", "Inter Display", system-ui, sans-serif', fontSize: '14px', fontWeight: 500, letterSpacing: '-0.02em', lineHeight: '18px' }}>
          Dossier
        </div>
        <div style={{ alignItems: 'center', backgroundColor: '#FFFFFF14', borderRadius: '6px', boxSizing: 'border-box', display: 'flex', flexShrink: 0, gap: '8px', height: '32px', paddingInline: '10px' }}>
          <svg width="16" height="16" viewBox="0 0 256 256" xmlns="http://www.w3.org/2000/svg" style={{ opacity: '0.5', flexShrink: '0', display: 'inline-block' }}>
            <path d="M216,68H133.39l-26-29.29a20,20,0,0,0-15-6.71H40A20,20,0,0,0,20,52V200.62A19.41,19.41,0,0,0,39.38,220H216.89A19.13,19.13,0,0,0,236,200.89V88A20,20,0,0,0,216,68ZM44,56H90.61l14.2,16H44ZM212,196H44V96H212Z" fill="#FFFFFF" />
          </svg>
          <div style={{ boxSizing: 'border-box', color: '#FFFFFF', display: 'inline-block', flexBasis: '0px', flexGrow: '1', flexShrink: '1', fontFamily: '"InterDisplay-Medium", "Inter Display", system-ui, sans-serif', fontSize: '14px', fontWeight: 500, letterSpacing: '-0.02em', lineHeight: '18px' }}>
            Barre de favoris
          </div>
          <svg width="16" height="16" viewBox="0 0 256 256" xmlns="http://www.w3.org/2000/svg" style={{ opacity: '0.5', flexShrink: '0', display: 'inline-block' }}>
            <path d="M216.49,104.49l-80,80a12,12,0,0,1-17,0l-80-80a12,12,0,0,1,17-17L128,159l71.51-71.52a12,12,0,0,1,17,17Z" fill="#FFFFFF" />
          </svg>
        </div>
      </div>
      <div style={{ alignItems: 'center', boxSizing: 'border-box', display: 'flex', justifyContent: 'space-between', paddingTop: '4px' }}>
        <div style={{ alignItems: 'center', borderRadius: '6px', boxSizing: 'border-box', display: 'flex', height: '32px', paddingInline: '10px' }}>
          <div style={{ boxSizing: 'border-box', color: '#FF7B6E', display: 'inline-block', fontFamily: '"InterDisplay-Medium", "Inter Display", system-ui, sans-serif', fontSize: '14px', fontWeight: 500, letterSpacing: '-0.02em', lineHeight: '18px' }}>
            Supprimer
          </div>
        </div>
        <div style={{ alignItems: 'center', backgroundColor: '#FFFFFF', borderRadius: '6px', boxSizing: 'border-box', display: 'flex', height: '32px', paddingInline: '14px' }}>
          <div style={{ boxSizing: 'border-box', color: '#171717', display: 'inline-block', fontFamily: '"InterDisplay-Medium", "Inter Display", system-ui, sans-serif', fontSize: '14px', fontWeight: 500, letterSpacing: '-0.02em', lineHeight: '18px' }}>
            Terminé
          </div>
        </div>
      </div>
      <div style={{ alignItems: 'center', boxSizing: 'border-box', display: 'flex', height: '24px', justifyContent: 'center', position: 'absolute', right: '8px', top: '8px', width: '24px' }}>
        <div style={{ alignItems: 'center', borderRadius: '6px', boxSizing: 'border-box', display: 'flex', flexShrink: 0, height: '24px', justifyContent: 'center', width: '24px' }}>
          <svg width="14" height="14" viewBox="0 0 224 224" xmlns="http://www.w3.org/2000/svg" style={{ opacity: '0.5', display: 'inline-block', flexShrink: 0 }}>
            <path d="M182.429 167.571a10.5 10.5 0 0 1-14.875 14.875L112 126.875 56.429 182.429a10.5 10.5 0 0 1-14.875-14.875L97.125 112 41.571 56.429a10.5 10.5 0 0 1 14.875-14.875L112 97.125l55.571-55.58a10.5 10.5 0 0 1 14.875 14.875L126.875 112Z" fill="#FFFFFF" />
          </svg>
        </div>
      </div>
    </div>
  )
