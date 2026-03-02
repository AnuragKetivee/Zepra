document.addEventListener('DOMContentLoaded', () => {
    // Register GSAP Plugins
    if (typeof gsap !== 'undefined') {
        gsap.registerPlugin(ScrollTrigger);
        initAnimations();
    }

    // Initialize Mobile Menu
    const mobileBtn = document.getElementById('mobile-menu-btn');
    const mobileMenu = document.getElementById('mobile-menu');
    
    if (mobileBtn && mobileMenu) {
        mobileBtn.addEventListener('click', () => {
            mobileMenu.classList.toggle('hidden');
            const isOpen = !mobileMenu.classList.contains('hidden');
            mobileBtn.setAttribute('aria-expanded', isOpen);
            
            // Animate Icon
            if(isOpen) {
               gsap.to(mobileMenu, {height: 'auto', duration: 0.3, ease: 'power2.out'});
            } else {
               gsap.to(mobileMenu, {height: 0, duration: 0.3, ease: 'power2.in'});
            }
        });
    }

    // Initialize Search
    const searchInput = document.getElementById('global-search');
    if (searchInput) {
        searchInput.addEventListener('input', (e) => handleSearch(e.target.value));
    }

    // Initialize Active LInk Highlighting
    highlightActiveLink();
    
    // Generate TOC
    generateTOC();
});

function initAnimations() {
    // Hero Section
    gsap.from('.hero-content > *', {
        y: 30,
        opacity: 0,
        duration: 0.8,
        stagger: 0.1,
        ease: 'power3.out'
    });

    // Content Cards / Sections
    const fadeElements = document.querySelectorAll('.animate-fade-up');
    fadeElements.forEach(el => {
        gsap.from(el, {
            scrollTrigger: {
                trigger: el,
                start: 'top 85%',
                toggleActions: 'play none none reverse'
            },
            y: 30,
            opacity: 0,
            duration: 0.6,
            ease: 'power2.out'
        });
    });
}

function handleSearch(query) {
    query = query.toLowerCase();
    const fileItems = document.querySelectorAll('.file-item, .card'); // Target both lists and cards
    
    fileItems.forEach(item => {
        const text = item.innerText.toLowerCase();
        if (text.includes(query)) {
            item.style.display = '';
            gsap.to(item, {opacity: 1, duration: 0.2});
        } else {
            item.style.display = 'none';
            item.style.opacity = 0;
        }
    });

    // Show/Hide sections based on visible children
    document.querySelectorAll('.doc-section').forEach(section => {
        const visibleChildren = Array.from(section.querySelectorAll('.file-item, .card'))
            .filter(child => child.style.display !== 'none');
        
        section.style.display = visibleChildren.length > 0 ? '' : 'none';
    });
}

function highlightActiveLink() {
    const currentPath = window.location.pathname.split('/').pop() || 'index.html';
    const navLinks = document.querySelectorAll('.nav-link');
    
    navLinks.forEach(link => {
        if (link.getAttribute('href') === currentPath) {
            link.classList.add('text-emerald-400', 'font-semibold');
            link.classList.remove('text-gray-400');
        }
    });
}

function generateTOC() {
    const tocContainer = document.getElementById('toc');
    if (!tocContainer) return;
    
    // Find all h2 and h3 in content
    const headers = document.querySelectorAll('.prose h2, .prose h3');
    if (headers.length === 0) {
        tocContainer.innerHTML = '<span class="text-xs text-gray-600 block pt-2">No sections</span>';
        return;
    }
    
    // Clear placeholder
    tocContainer.innerHTML = '';
    
    headers.forEach((header, index) => {
        // Add ID if missing
        if (!header.id) {
            header.id = 'section-' + index;
        }
        
        const link = document.createElement('a');
        link.href = '#' + header.id;
        link.textContent = header.innerText;
        link.className = 'block hover:text-white transition-colors truncate ' + 
                         (header.tagName === 'H3' ? 'pl-4 text-xs' : '');
        
        tocContainer.appendChild(link);
    });
}
