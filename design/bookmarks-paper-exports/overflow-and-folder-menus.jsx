// Paper export — get_jsx includeNodeIds:false, 2026-11-08.
// Comment c-99tvvr (overflow empty left gutter): every menu row now has a 16px icon in the
// same 8px-inset lane. Two action rows previously had an empty 24px slot (paddingLeft 32):
//   « Gérer les favoris »    -> Phosphor Bold Star (outline), white 70%  (same glyph family as the address-bar star)
//   « Ouvrir les N favoris » -> Phosphor Bold ArrowSquareOut, white 70%
// Both rows: paddingLeft 8, gap 8, icon 16×16 flexShrink 0 — identical lane to favicon/folder rows.

// ── A. Artboard 10 « Débordement et fenêtre étroite » — "Menu · Débordement" (chevron »)
(
    <div style={{ backgroundColor: '#171717', borderColor: '#FFFFFF0F', borderRadius: '10px', borderStyle: 'solid', borderWidth: '1px', boxShadow: '#00000066 0px 8px 24px', boxSizing: 'border-box', display: 'flex', flexDirection: 'column', fontSize: '12px', fontSynthesis: 'none', lineHeight: '16px', MozOsxFontSmoothing: 'grayscale', overflowWrap: 'anywhere', padding: '4px', WebkitFontSmoothing: 'antialiased', width: '300px' }}>
      <div style={{ alignItems: 'center', borderRadius: '6px', boxSizing: 'border-box', display: 'flex', flexShrink: 0, gap: '8px', height: '32px', paddingLeft: '8px', paddingRight: '10px' }}>
        <div style={{ backgroundColor: '#B4E0FF', borderRadius: '4px', boxSizing: 'border-box', flexShrink: '0', height: '16px', width: '16px' }} />
        <div style={{ boxSizing: 'border-box', color: '#FFFFFFE6', display: 'inline-block', flexBasis: '0px', flexGrow: '1', flexShrink: '1', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '14px', lineHeight: '20px' }}>
          SNCF Connect
        </div>
      </div>
      <div style={{ alignItems: 'center', borderRadius: '6px', boxSizing: 'border-box', display: 'flex', flexShrink: 0, gap: '8px', height: '32px', paddingLeft: '8px', paddingRight: '10px' }}>
        <div style={{ backgroundColor: '#000091', borderRadius: '4px', boxShadow: '#FFFFFF33 0px 0px 0px 1px inset', boxSizing: 'border-box', flexShrink: '0', height: '16px', width: '16px' }} />
        <div style={{ boxSizing: 'border-box', color: '#FFFFFFE6', display: 'inline-block', flexBasis: '0px', flexGrow: '1', flexShrink: '1', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '14px', lineHeight: '20px' }}>
          Légifrance
        </div>
      </div>
      <div style={{ alignItems: 'center', borderRadius: '6px', boxSizing: 'border-box', display: 'flex', flexShrink: 0, gap: '8px', height: '32px', paddingLeft: '8px', paddingRight: '10px' }}>
        <svg width="16" height="16" viewBox="0 0 256 256" xmlns="http://www.w3.org/2000/svg" style={{ opacity: '0.7', flexShrink: '0', display: 'inline-block' }}>
          <path d="M216,68H133.39l-26-29.29a20,20,0,0,0-15-6.71H40A20,20,0,0,0,20,52V200.62A19.41,19.41,0,0,0,39.38,220H216.89A19.13,19.13,0,0,0,236,200.89V88A20,20,0,0,0,216,68ZM44,56H90.61l14.2,16H44ZM212,196H44V96H212Z" fill="#FFFFFF" />
        </svg>
        <div style={{ boxSizing: 'border-box', color: '#FFFFFFE6', display: 'inline-block', flexBasis: '0px', flexGrow: '1', flexShrink: '1', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '14px', lineHeight: '20px' }}>
          Recettes
        </div>
        <svg width="12" height="12" viewBox="0 0 12 12" xmlns="http://www.w3.org/2000/svg" style={{ display: 'inline-block', flexShrink: 0 }}>
          <path d="M4 2L8 6L4 10" fill="none" stroke="#FFFFFFB3" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
      </div>
      <div style={{ alignItems: 'center', boxSizing: 'border-box', display: 'flex', flexShrink: 0, height: '9px', paddingInline: '8px' }}>
        <div style={{ backgroundColor: '#FFFFFF0F', boxSizing: 'border-box', flexBasis: '0px', flexGrow: '1', flexShrink: '1', height: '1px' }} />
      </div>
      <div style={{ alignItems: 'center', borderRadius: '6px', boxSizing: 'border-box', display: 'flex', flexShrink: 0, gap: '8px', height: '32px', paddingLeft: '8px', paddingRight: '10px' }}>
        <svg width="16" height="16" viewBox="0 0 256 256" xmlns="http://www.w3.org/2000/svg" style={{ opacity: '0.7', flexShrink: '0', display: 'inline-block' }}>
          <path d="M216,68H133.39l-26-29.29a20,20,0,0,0-15-6.71H40A20,20,0,0,0,20,52V200.62A19.41,19.41,0,0,0,39.38,220H216.89A19.13,19.13,0,0,0,236,200.89V88A20,20,0,0,0,216,68ZM44,56H90.61l14.2,16H44ZM212,196H44V96H212Z" fill="#FFFFFF" />
        </svg>
        <div style={{ boxSizing: 'border-box', color: '#FFFFFFE6', display: 'inline-block', flexBasis: '0px', flexGrow: '1', flexShrink: '1', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '14px', lineHeight: '20px' }}>
          Autres favoris
        </div>
        <svg width="12" height="12" viewBox="0 0 12 12" xmlns="http://www.w3.org/2000/svg" style={{ display: 'inline-block', flexShrink: 0 }}>
          <path d="M4 2L8 6L4 10" fill="none" stroke="#FFFFFFB3" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
      </div>
      <div style={{ alignItems: 'center', borderRadius: '6px', boxSizing: 'border-box', display: 'flex', flexShrink: 0, gap: '8px', height: '32px', paddingLeft: '8px', paddingRight: '10px' }}>
        <svg width="16" height="16" viewBox="0 0 256 256" xmlns="http://www.w3.org/2000/svg" style={{ opacity: '0.7', flexShrink: '0', display: 'inline-block' }}>
          <path d="M243,96a20.33,20.33,0,0,0-17.74-14l-56.59-4.57L146.83,24.62a20.36,20.36,0,0,0-37.66,0L87.35,77.44,30.76,82A20.45,20.45,0,0,0,19.1,117.88l43.18,37.24-13.2,55.7A20.37,20.37,0,0,0,79.57,233L128,203.19,176.43,233a20.39,20.39,0,0,0,30.49-22.15l-13.2-55.7,43.18-37.24A20.43,20.43,0,0,0,243,96ZM172.53,141.7a12,12,0,0,0-3.84,11.86L181.58,208l-47.29-29.08a12,12,0,0,0-12.58,0L74.42,208l12.89-54.4a12,12,0,0,0-3.84-11.86L41.2,105.24l55.4-4.47a12,12,0,0,0,10.13-7.38L128,41.89l21.27,51.5a12,12,0,0,0,10.13,7.38l55.4,4.47Z" fill="#FFFFFF" />
        </svg>
        <div style={{ boxSizing: 'border-box', color: '#FFFFFFE6', display: 'inline-block', flexBasis: '0px', flexGrow: '1', flexShrink: '1', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '14px', lineHeight: '20px' }}>
          Gérer les favoris
        </div>
        <div style={{ boxSizing: 'border-box', display: 'flex', gap: '2px' }}>
          <div style={{ backgroundColor: '#FFFFFF0F', borderRadius: '4px', boxSizing: 'border-box', paddingBlock: '2px', paddingInline: '6px' }}>
            <div style={{ boxSizing: 'border-box', color: '#FFFFFF52', display: 'inline-block', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '14px', lineHeight: '16px' }}>
              Ctrl
            </div>
          </div>
          <div style={{ backgroundColor: '#FFFFFF0F', borderRadius: '4px', boxSizing: 'border-box', paddingBlock: '2px', paddingInline: '6px' }}>
            <div style={{ boxSizing: 'border-box', color: '#FFFFFF52', display: 'inline-block', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '14px', lineHeight: '16px' }}>
              Maj
            </div>
          </div>
          <div style={{ backgroundColor: '#FFFFFF0F', borderRadius: '4px', boxSizing: 'border-box', paddingBlock: '2px', paddingInline: '6px' }}>
            <div style={{ boxSizing: 'border-box', color: '#FFFFFF52', display: 'inline-block', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '14px', lineHeight: '16px' }}>
              O
            </div>
          </div>
        </div>
      </div>
    </div>
  )

// ── B. Artboard 05 « Menu de dossier et sous-menu » — folder menu, last row (only changed row shown)
(
      <div style={{ alignItems: 'center', borderRadius: '6px', boxSizing: 'border-box', display: 'flex', flexShrink: 0, gap: '8px', height: '32px', paddingLeft: '8px', paddingRight: '10px' }}>
        <svg width="16" height="16" viewBox="0 0 256 256" xmlns="http://www.w3.org/2000/svg" style={{ opacity: '0.7', flexShrink: '0', display: 'inline-block' }}>
          <path d="M228,104a12,12,0,0,1-24,0V69l-59.51,59.51a12,12,0,0,1-17-17L187,52H152a12,12,0,0,1,0-24h64a12,12,0,0,1,12,12Zm-44,24a12,12,0,0,0-12,12v64H52V84h64a12,12,0,0,0,0-24H48A20,20,0,0,0,28,80V208a20,20,0,0,0,20,20H176a20,20,0,0,0,20-20V140A12,12,0,0,0,184,128Z" fill="#FFFFFF" />
        </svg>
        <div style={{ boxSizing: 'border-box', color: '#FFFFFFE6', display: 'inline-block', flexBasis: '0px', flexGrow: '1', flexShrink: '1', fontFamily: '"InterDisplay-Regular", "Inter Display", system-ui, sans-serif', fontSize: '14px', lineHeight: '20px' }}>
          Ouvrir les 6 favoris
        </div>
      </div>
  )
