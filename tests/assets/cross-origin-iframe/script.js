window.addEventListener('load', () => {
    parent.postMessage({
        script: 'external-script-loaded',
        color: getComputedStyle(document.body).color
    }, 'app://iframe-main');
});
