import type { Metadata } from "next";
import { Analytics } from "@vercel/analytics/next";
import "./globals.css";

export const metadata: Metadata = {
  title: "photogod — photoshop, but small, fast, and it runs on linux",
  description:
    "photogod is a small, fast image editor for mac, windows and linux. free.",
  openGraph: {
    title: "photogod",
    description: "photoshop, but small, fast, and it runs on linux",
    type: "website",
  },
};

/** runs before paint so the theme never flashes */
const themeScript = `
(function(){try{
var s=localStorage.getItem('pg-theme');
var d=s?s==='dark':matchMedia('(prefers-color-scheme: dark)').matches;
if(d)document.documentElement.classList.add('dark');
}catch(e){}})();
`;

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" className="h-full antialiased" suppressHydrationWarning>
      <head>
        <script dangerouslySetInnerHTML={{ __html: themeScript }} />
      </head>
      <body className="flex min-h-full flex-col">
        {children}
        <Analytics />
      </body>
    </html>
  );
}
